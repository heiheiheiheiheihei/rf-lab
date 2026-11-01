# 04 · 设备 ↔ App 协议契约
状态:`draft` · 契约版本 `v1.1` · 帧版本 `VER = 0x01` · 最后更新:2026-10-31
> **本文件是协议契约的唯一权威来源。** 固件、App、AI 提示词三方均以此为准;**任何字段改动必须先改本文件**,再在同一批改动里同步 `protocol/schemas/`。机器可读镜像见 [`protocol/schemas/`](../protocol/schemas/)(校验 `python3 tools/validate_schemas.py`);固件实现见 [03-firmware.md](03-firmware.md),App 与 AI 见 [05-app-ai.md](05-app-ai.md),验证用例见 [06-test-plan.md](06-test-plan.md)。
> **章节编号 §1–§9 及子节号被 schema 与代码直接引用,不得重排或复用。**

## 1. 设计原则

| 原则 | 理由 |
|---|---|
| **一份帧格式,三条传输** | Wi-Fi(WebSocket)、BLE 与 USB 共用同一套消息语义,避免多套逻辑分叉 |
| **控制用 JSON,数据用二进制** | 控制消息可读、可调试;捕获数据体积大,必须走二进制分片,不走 base64+JSON |
| **设备不持有密钥** | 设备只回传特征;DeepSeek API Key 仅存于手机(App 侧) |
| **发射默认拒绝** | 一切发射需频段白名单 + 功率上限 + 显式授权,由软件强制而非文档约定 |
| **能力协商优先** | App 启动先读能力集,不假设模块存在;多模块选型由用户配置驱动 |
| **前向兼容** | 未知字段忽略,未知消息类型回 `ERROR{unsupported}`,不崩溃 |

## 2. 帧格式
三条传输共用同一帧头(小端),**头后内容按通道类型解析**:

```text
 0        1        2        3        4        5
+--------+--------+--------+--------+--------+--------...
| MAGIC  | VER    | TYPE   | SEQ    | LEN(2B) | PAYLOAD
| 0x52   | 0x01   | u8     | u8     | u16 LE  | LEN bytes
+--------+--------+--------+--------+--------+--------...
```

| 字段 | 长度 | 说明 |
|---|---|---|
| `MAGIC` | 1B | 固定 `0x52`('R'),用于重新同步 |
| `VER` | 1B | 协议主版本,当前 `0x01`。不匹配即 `ERROR{version_mismatch}` |
| `TYPE` | 1B | 消息类型,见 §3。最高位 `0x80` 标记**二进制数据帧** |
| `SEQ` | 1B | 序列号,每帧递增,用于配对请求/响应与丢帧检测 |
| `LEN` | 2B LE | `PAYLOAD` 字节长度。**`LEN` 的实际上限由承载决定**:USB = `CAPS.limits.usb_frame_max`(本固件 16384,§2.2.2),BLE = `CAPS.limits.ble_frame_max`(§2.2.1),Wi-Fi 可达 65535。`4096` **只是 §2.2.1 BLE 分片预算推导中沿用的格式层参考值**,不是 USB 上限、也不是任何承载的独立上限——USB 的 16384 与它不矛盾。超限一律回 `ERROR{frame_too_large}` 并给出当前承载上限(§6) |

### 2.1 PAYLOAD 解析规则
控制帧(`TYPE` 最高位 = 0)的 `PAYLOAD` 是 UTF-8 JSON 文本;数据帧(`TYPE` 最高位 = 1)是 §3.5 定义的二进制块 + 尾部 JSON 元数据。

### 2.2 传输映射
**三条传输共用同一套帧格式与全部消息语义。** 这是本契约的核心约定:新增一条传输**不新增任何消息类型**。⚠️ BLE 与 Wi-Fi 是**移出本批(⏸ 延后)而非删除**——承载定义、`ble_frame_max` 推导与分片规则全部保留,后续批次直接接续,不得删字段或重推导。

| 通道 | 承载 | 参数 | 本批 |
|---|---|---|---|
| **Wi-Fi** ⏸ | WebSocket 二进制帧(1 帧 = 1 个 RF-Lab 帧) | 设备开 AP 或连现场路由器;`ws://<ip>:8266/v1` | ⏸ 移出本批,定义保留 |
| **BLE** ⏸ | GATT Notify / Write,自定义 Service | 单帧超 MTU 时按 §2.3 分片;可达上限见 §2.2.1 | ⏸ 移出本批,定义保留 |
| **USB** | **CDC-ACM 字节流**(手机为 USB 主机,设备为从设备) | 帧语义与物理通路解耦;本批实际通路 = 板载 **CH343P**(VID:PID `1a86:55d3`)→ UART0,原生 USB 在本板不可用;上限见 §2.2.2 | ✅ **本批唯一承载** |

> ⏸ **活动链路唯一性与抢占(本批不适用,定义保留)**:同一时刻只有一条链路承载会话;新链路完成 `HELLO` 时旧链路被隐式中止并回 `WARN{preempted}`;**USB 可抢占正在使用的 Wi-Fi/BLE 链路**;禁止两条链路同时下发命令(否则 `seq` 防重放校验与状态机抢占出现竞态)。本批只有 USB 一条链路,不存在仲裁对象,**不要实现抢占分支**。

### 2.2.1 BLE 上"一个完整帧"能有多大(⏸ 本批不实现,定义与限制值保留)
本小节推导只用 `LEN = 4096` 作为**格式层参考值**(历史设计值,非任何承载的实际上限;USB 的实际上限是 §2.2.2 的 `usb_frame_max` = 16384)。即便如此,**字段能表示 ≠ 能传过去**:整帧必须被切成 ≤15 个分片(§2.3 的 4bit `FRAG_TOTAL`),每片能装多少由协商出的 ATT 载荷决定。按 BLE 5.0 数据长度扩展的典型值推导:

```text
LL_Payload(251) − L2CAP 头(4)           = ATT_Payload 247
每片占用 = ATT 操作码(1) + FRAG 头(1)   = 2  ⇒ 每片净载 245B
可重组整帧上限 = 245 × 15               = 3675B
LEN 上限 = 3675 − 帧头 6                = 3669B
```
**结论:BLE 上 `LEN > 3669` 的帧在数学上无法完整分片传输**(与格式层 4096 差 **427B**);`3669` 是**上界不是目标**——实际 ATT 载荷由协商决定(未启用数据长度扩展时仅 23B),协商越小可重组整帧越短。**口径备忘**:`247` 是 **ATT 载荷**(= LL 载荷 251 − L2CAP 头 4),常用口径 "MTU 244 / 247" 属不同层级,本契约统一取 **247**;每片只扣一次操作码与 FRAG 头,帧头 6B 全帧只出现一次,**不要**在 245 里再减帧头。
因此规定:**`CAPS.limits.ble_frame_max` 是唯一真实来源**(设备按实际协商的 ATT 载荷算出并上报),App **必须**用它决定请求规模,不得硬编码 4096 或 3669;保守设计建议 BLE 路径单帧 `LEN ≤ 3600`,Wi-Fi 路径可达 65535;设备收到**已知超出当前承载上限**的帧回 `ERROR{frame_too_large}` 并给出上限值,**禁止静默截断**(截断的捕获数据会被下游误当作完整信号);`DATA_CHUNK` 的**分块上限与承载相关**,设备按 `ble_frame_max` / `usb_frame_max` 自适应选块大小,App 侧不得假设固定块大小。

### 2.2.2 USB 上"一个完整帧"能有多大
USB CDC 是**包边界明确的字节流**,没有 BLE 那样的 20–244B 分片约束:因此**不需要 `FRAG` 头**(§2.3 的分片机制**只适用于 BLE**),上限取 `CAPS.limits.usb_frame_max`(由设备按自身缓冲给出)。虽无协议层小上限,**仍不得**把整次捕获塞进一帧——捕获数据必须按 `DATA_CHUNK` 分块(块 ≤ `usb_frame_max`),以便 App 增量展现进度、任一侧缓冲不溢出;超限一律回 `ERROR{frame_too_large}`(与 BLE 同规),**禁止静默截断**。

### 2.3 分片(BLE 必需,⏸ 本批不实现)
BLE ATT MTU 协商后有效载荷通常 20–244B。分片头:`[FRAG_IDX:4bit | FRAG_TOTAL:4bit]`,**总长 1 字节**。
> **`FRAG 头` 是 1 字节,不是 2 字节。** 两个字段各占 4bit,恰好塞满 1 字节:`FRAG_IDX` 取 0–15,`FRAG_TOTAL` 取 1–15。按"2 字节头"实现的固件会与 App 侧错位。
`FRAG_TOTAL = 1` 时省略 FRAG 头,直接发原帧(兼容 Wi-Fi 路径);接收端按 `SEQ + FRAG_IDX` 重组,**缺片超时 500ms** 即丢弃整帧并回 `ERROR{frag_timeout}`。

### 2.4 分片之外的流控
设备→App 的 `SCAN_HIT` 与 `DATA_CHUNK` 是高频消息,**必须**受令牌桶限流(默认 **50 msg/s**,见 `CAPS.limits.stream_rate_hz`);超限时设备丢**旧**数据保**新**数据,并发 `WARN{throttled}` 计数。
> ⚠️ **限流不得吃掉"覆盖诚实性"。** 用 25kHz 步进扫 779–928MHz 全带有 5960 个点,低阈值下命中可达数千条,按 50 msg/s 发不完;此时若悄悄丢命中,App 会误判"这段频谱很干净"。因此 §3.3 的 `SCAN_DONE` 必须携带完整命中统计与丢弃计数——**限流只影响实时推送的及时性,不影响结果的完整性声明**;需要全部明细时用 `GET_SCAN_HITS` 分页取回。

## 3. 消息类型

### 3.1 会话与能力

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x01` | `HELLO` | App→设备 | 必带 `{app_version, proto_min, proto_max, client_id}`,**client_id 必须非空且 ≤64 字节**(配对时固件对空值/超长回 `ERROR{unsupported}`,§7.1);`pairing` 缺省或 `false` = **只建立会话、不请求配对**(自检工具默认如此,不写 NVS),`pairing:true` = 请求配对(§7.1,可能永久绑定) |
| `0x81` | `HELLO_ACK` | 设备→App | `{fw_version, proto, device_id, chip:"esp32s3", uptime_ms, last_accepted_seq}`;配对字段见 §7.1 |
| `0x02` / `0x82` | `GET_CAPS` / `CAPS` | App→设备 / 设备→App | 请求 `{}`;应答见 §4 |
| `0x03` / `0x83` | `PING` / `PONG` | 双向 | `{t_ms}`,用于 RTT 与保活(默认 5s) |
| `0x7F` / `0x7E` | `ERROR` / `WARN` | 设备→App | `ERROR{code, msg, seq_ref}` / `WARN{code, msg}`(码表见 §6) |

### 3.2 模块管理

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x10` / `0x90` | `LIST_MODULES` / `MODULES` | App→设备 / 设备→App | 请求 `{}`;应答 `[{slot, chip, present, bands, tx_max_dbm, driver_version, spi:{bus,miso,mosi,sck,cs}, ant:"SMA"}]` |
| `0x11` / `0x91` | `SELECT_MODULE` / `MODULE_STATE` | App→设备 / 设备→App | 请求 `{slot}`(切换当前活动模块);状态 `{slot, state:"ready"\|"busy"\|"absent"\|"error", detail}` |
> **约束**:同一时刻只有一个**活动**模块可收发。切换模块会 `STOP_SCAN` / `ABORT_CAPTURE`,并丢弃未完成的捕获。
**`spi` 字段语义**(硬件为"单 SPI2 总线 + 每模块独立 CS"):`bus` = 主机 SPI 控制器序号,**本项目恒为 `2`**(SPI2_HOST);`miso`/`mosi`/`sck` = 该总线引脚号,**三个模块上报值相同**(物理上共用同一组线,正常且预期);`cs` = 该模块片选引脚,**三模块各不相同**,是唯一区分项。即:`(bus, cs)` 联合唯一标识一个模块;`bus` 相同**不代表**模块冲突,`cs` 相同才是配置错误,App 不应把"三模块 `bus` 相同"当作异常报警。

### 3.3 扫频与 RSSI

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x20` | `START_SCAN` | App→设备 | `{f_start_hz, f_stop_hz, step_hz, dwell_ms, rssi_threshold_dbm, mode:"peak"\|"sweep"}` |
| `0xA0` | `SCAN_HIT` | 设备→App | `{scan_id, f_hz, rssi_dbm, t_ms}` — 仅回超阈值的点;`t_ms` 为设备**单调 uptime**(§3.6.1) |
| `0xA1` | `SCAN_PROGRESS` | 设备→App | `{f_cur_hz, pct, elapsed_ms, hits}` |
| `0x21` | `STOP_SCAN` | App→设备 | `{}` |
| `0xA2` | `SCAN_DONE` | 设备→App | `{hits_total, hits_pushed, hits_dropped, elapsed_ms, aborted:bool, coverage:[{f_start_hz,f_stop_hz}...]}` |
| `0x23` / `0xA4` | `GET_SCAN_HITS` / `SCAN_HITS` | App→设备 / 设备→App | 请求 `{scan_id, offset, limit}`(**分页取回**上次扫描的完整命中明细);应答 `{scan_id, total, offset, hits:[{f_hz,rssi_dbm,t_ms}...]}` |
| `0x22` / `0xA3` | `SET_RSSI_STREAM` / `RSSI_SAMPLE` | App→设备 / 设备→App | 请求 `{f_hz, interval_ms, enable}`(定点实时 RSSI);应答 `{f_hz, rssi_dbm, t_ms}` |
**`SCAN_DONE` 计数语义**(三者必须自洽,否则属设备实现缺陷):`hits_total` = 本次扫描**实际检测到**的超阈值点数,一个不落;`hits_pushed` = 因限流与分页而**已通过 `SCAN_HIT` 推送**给 App 的条数;`hits_dropped` = `hits_total − hits_pushed`,即需要在限流窗口内被丢弃的条数。
只需**结论**时,App 读 `hits_total` 与 `hits_dropped`,无需取明细;需要**明细**时用 `GET_SCAN_HITS` 按 `scan_id` 分页拉取,命中缓存在设备端保留到**下一次扫描开始**。**禁止**把 `hits_dropped > 0` 当成正常结束而不告知用户——App 发现该值非零时必须提示"本次明细不完整",避免把"没推送"误读为"没信号"。
> **覆盖诚实性**:`SCAN_DONE.coverage` 必须回传**真实扫过的频段**。若请求范围超出模块能力,设备不得静默跳过——必须回 `WARN{band_clipped}` 并给出实际覆盖区间。

### 3.4 捕获与原始数据

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x30` | `CAPTURE_START` | App→设备 | `{f_hz, mod:"ook"\|"2fsk"\|"auto", max_edges, timeout_ms, trigger_rssi_dbm}` |
| `0xB0` | `DATA_CHUNK` | 设备→App | **二进制帧**,见 §3.5 |
| `0xB1` | `CAPTURE_DONE` | 设备→App | `{edges, duration_ms, truncated:bool, features:{...}}` |
| `0x31` | `CAPTURE_ABORT` | App→设备 | `{}` |
**`max_edges` 冲突语义(必须明确,否则两端行为不可预测)**:当 `CAPTURE_START.max_edges` 大于 `CAPS.limits.max_edges` 时,设备**钳制到后者并继续捕获**,同时发 `WARN{edge_clamped}`,并把 `CAPTURE_DONE.truncated` 置 `true`。**不得报错拒绝**——钳制是信息无损的降级(截断事实已由 `truncated` 显式表达)。三层防线必须同源:契约钳制值 = `CAPS.limits.max_edges` = 固件编译期硬上限;App 应先读 `CAPS` 再决定请求值,把钳制当作异常路径。

### 3.5 二进制载荷布局(`TYPE` 最高位 = 1)
原始捕获的价值在**时序**,所以载荷是变长 RLE,而不是采样数组:

```text
+--------+------------------+---------------------------+
| FLAGS  | edge[0] 起点     | RLE 对 (dt_us, level) ... |
| 1B     | u32 LE 相对微秒  | 每对: u16 LE dt | u8 level |
+--------+------------------+---------------------------+
尾部: 0x00 0x00 后接 UTF-8 JSON 元数据
```

| 项 | 说明 |
|---|---|
| `FLAGS` bit0 | 载荷语义:`0` = OOK/ASK 电平序列,`1` = FSK 过零/相位序列 |
| `FLAGS` bit1 | **判决点**:`0` = **未经判决**的模拟前端数字化序列(比较器/过零整形输出,**保留时序**),`1` = **芯片内部已判决**的位流(时钟已恢复,时序信息已丢失) |
| `dt_us` | 与上一跳变的间隔(微秒)。饱和值 `0xFFFF` 表示"超长间隔",真实值在元数据 `long_gaps[]` 里 |
| `level` / 元数据 | `level` 取 `0`/`1`;元数据 JSON 为 `{f_hz, mod, rssi_dbm, sample_clock_hz, long_gaps:[...]}` |
> **为什么 bit1 必须存在**:同是"FSK 数据",`bit0=1,bit1=0` 是**过零序列**(跳变间隔为载波周期级,可反推频偏),`bit0=1,bit1=1` 是**芯片解调后的位流**(跳变间隔为符号率级,已无频偏信息)。两者的 `pulse_width_us` 直方图、`symbol_rate_bps` 甚至 `edge_count` **量纲完全不同**,混为一谈会让 AI 把"过零点间隔"当成"符号宽度"从而误判协议。bit1 缺省(`0`)表示"未声明的模拟前端序列",是保守取值。另见 §3.4:`mod:"2fsk"` 有一条**软件补不了的硬件前提**——RMT 只采数字边沿,2FSK 必须先经比较器/过零整形才能进入捕获路径(实现见 [03-firmware.md](03-firmware.md))。典型体积:1000 跳变 ≈ 3KB,足以覆盖绝大多数 433MHz 遥控器一帧。

### 3.6 特征提取(设备侧,供 AI 消费)
设备把原始捕获**归纳成 `features`**,这是发给 DeepSeek 的唯一信号载荷(避免上传原始码流)。`pulse_width_us` 结构为 `{min, max, hist}`(μs;`hist` 是 0..1 的比例数组)。
- **测量量**(可直接测):`f_hz` `mod` `rssi_dbm` `edge_count` `duration_ms` `pulse_width_us` `noise_floor_dbm` `frame_repeats` —— 直接给值,无需置信度。
- **估计量**(算法给出,可能不准):`symbol_rate_bps` `repetition_ms` —— 必带同名置信度 `symbol_rate_confidence`、`repetition_ms_confidence`。
- **猜测量**(本质是猜测):`encoding_guess` `payload_bits` `preamble_bits` `crc_guess` —— 必带 `encoding_confidence` `payload_bits_confidence` `preamble_bits_confidence` `crc_confidence`;**证据不足时必须把值置 `null`**,而不是给一个低置信度的胡猜。
规则(强制):
1. `*_confidence` 取 `0.0`–`1.0`(`0` = 纯猜测,`1` = 确定),**或 `null`**;`null` 与"字段缺失"语义**不同**(规则 5)。
2. 任何猜测量**允许为 `null`**,语义是"**设备判定无足够证据,拒绝猜测**",与"猜错了的值"是两回事。
3. `incomplete`:`true` 表示本次特征基于**被截断**(`CAPTURE_DONE.truncated=true`)或**丢帧**的数据算出 → **AI 必须降低结论强度,App 必须显式标注"数据不完整"**。
4. `raw_semantics` 镜像 §3.5 的 `FLAGS` bit1,三选一:`undecided`(未判决的模拟前端数字化序列,保留时序)/ `decided`(芯片内部已判决的位流,时序信息已丢失)/ `unknown`(设备无法判断,如固件未接判决点信息或该驱动不提供)。它决定 `symbol_rate_bps` 与 `pulse_width_us` 该怎么解读;设备**不得**在无法判断时默认填 `"undecided"`——那等于谎报载荷语义。
5. **字段缺失的降级规则(两端同源)**:`*_confidence: 0.4` = 设备给了估计且自知不确定,按该值权衡、`< 0.5` 时结论必须标为弱推断;`*_confidence: null` = 设备**明确拒绝**给置信度,与"缺失"同等待遇但可断言设备支持 v1.1;**字段缺失** = 设备**可能不支持 v1.1**,一律按"**未知/低**"处理,**绝不默认当真**,不得推断为 1.0。**核心原则:缺失 = 未知,不等于"确定"。**
> **为什么这比"多几个字段"重要**:`crc_guess` 的示例值 `"crc8_ poly?0x07"` 里那个 `?` 就是不确定性的赤裸痕迹——塞进一个 `string` 传给大模型,模型会当成**事实**读;给出 `crc_confidence: 0.22` 之后,模型才有可能说"疑似 CRC8,但置信度低,建议进一步验证"。**这是防止 AI 幻觉的第一道闸门,且必须在设备侧完成**,因为设备是唯一知道证据强弱的角色。

### 3.6.1 时间基准(强制单一口径)
- `t_ms`(`SCAN_HIT` / `RSSI_SAMPLE`):设备**单调 `uptime_ms`**(自本次上电起算)。**不是**绝对时间——设备无 RTC,不提供 Unix 时间;跨重启不可比较。App 若需绝对时间,须自行记录"握手时刻 + uptime 偏移"。
- `sample_clock_hz`:**采样时钟频率**,不是时间戳。与 `dt_us` 相乘可把 RLE 还原成采样点序号,与挂钟时间无关。
> **为什么必须写死基准**:若一端当绝对时间、另一端当 uptime,所有"重复周期/占空比"的跨会话比较都会静默错误。设备**不提供**绝对时间,任何需要绝对时间的结论(App 报告、审计导出)都必须由 App 侧补。

### 3.7 发射与动作

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x40` | `TX_REQUEST` | App→设备 | `{f_hz, mod:"ook"\|"2fsk", payload_rle, repeats, power_dbm, auth_token, reason}`;两段式发射的**第二次确认**复用本消息并**只带** `{armed_ack:true}`。**`mod` 不含 `"auto"`**:`auto` 是接收侧的自动选择语义(§3.4),发射必须由 App 指定具体调制 |
| `0xC0` | `TX_STATUS` | 设备→App | `{state:"denied"\|"armed"\|"sent"\|"failed", code, detail, granted_power_dbm}` |
| `0x41` | `TX_ABORT` | App→设备 | `{}` |
| `0x42` | `SAVE_SIGNAL` | App→设备 | `{name, features, raw_ref}` — 存入设备本地 |
| `0x43` | `REPLAY_SIGNAL` | App→设备 | `{name, repeats, auth_token}` |

**两段式 `TX_REQUEST` 的形态互斥(机器可读镜像用 `oneOf` 强制)**:第一阶段必须带完整参数 `{f_hz, mod, payload_rle, repeats, power_dbm, reason}`(按 `policy.tx_requires_token` 可带 `auth_token`),**不得带 `armed_ack`**;第二阶段**只带** `{armed_ack:true}`,不得携带任何其它字段。把两阶段混在同一帧、或第二阶段夹带参数,都属非法载荷,设备应拒绝(§6 无专门的「载荷格式错」错误码,这是已知缺口;当前用 `ERROR{unsupported}` 并说明原因)。

**`payload_rle` 的编码(App↔设备两个方向必须一致)**:两个方向复用 §3.5 的同一布局(`FLAGS` 1B + `edge[0]` 起点 u32 LE + RLE 对 `u16 LE dt` / `u8 level`)。在 **JSON 消息**里该结构以 **base64url 字符串**承载(因为控制帧 `PAYLOAD` 必须是 UTF-8 JSON,§2.1),目前**只有 `TX_REQUEST.payload_rle`** 承载原始码流。`REPLAY_SIGNAL` **不承载**该结构:它按 `name` 引用设备本地已存信号,故载荷只有 `{name, repeats, auth_token}`——回放码流不经过 App,省带宽且避免 App 侧篡改。在 **`DATA_CHUNK`**(`0xB0`)里,同一结构直接作为二进制块承载。**统一布局的理由**:App 常做"捕获→回放"闭环,两个方向布局不同就必须做一次无谓且易错的转码。`FLAGS` 的 bit1(判决点)在**发射**方向无意义(发射的是电平序列),发送方置 `0`。
**`TX_STATUS.state = "armed"` 的语义**

| 状态 | 含义 | 后续 |
|---|---|---|
| `denied` | 门禁未通过,未发射 | 终态;按 `code` 提示,**不得重试** |
| `armed` | 门禁**全部通过**,设备已进入发射就绪态但**尚未开始辐射** | 设备在 `CAPS.limits.arm_timeout_ms`(建议 5000)内若未收到 App 的**再次确认**(第二次 `TX_REQUEST`,**只带** `{armed_ack:true}`),则**自动回落**为 idle 并回 `TX_STATUS{state:"failed", code:"arm_timeout"}`。该确认只属第二阶段,第一阶段仍须带完整参数与令牌 |
| `sent` | 已发射完成(含 `repeats` 全部重复) | 终态 |
| `failed` | 已授权但发射过程失败(超时/硬件错误) | 终态;写审计 |
> **`armed` 是"两段式发射"的中间态**,作用是给用户一次"最后一次反悔"的机会。它**不是**"正在发射"——这段时间内**射频必须零输出**(测试须断言 `tx()` 调用次数 = 0)。
**`TX_STATUS.granted_power_dbm`(可选字段)**:类型 `number`(可带一位小数);**出现时机**为 `state ∈ {armed, sent, failed}` 时**必须**给出,`denied` 时可省略(未发射即无"生效值",给出会暗示"发过了")。**语义**:本次发射**实际生效**的功率,即 `min(TX_REQUEST.power_dbm, policy.tx_power_cap_dbm, 该模块 tx_max_dbm)`;夹取的判定与计算在**设备侧**,App 无权重算。**与请求值的关系**:`granted_power_dbm` **优先于** App 本地记录的请求值,App **必须**用它在确认弹窗/结果卡片/审计镜像里显示,而**不是**显示自己发出去的数字——`M2(AS07)` 的 `tx_max_dbm` 是 10,若 App 请求 20 而界面显示 20,用户会以为"已在 20dBm 发射",**实际只发了 10dBm**,这是"静默谎报";有了该字段,UI 才能显示"**实际 10dBm(受模块上限限制,非策略限制)**"。

### 3.8 存储与系统

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x50` / `0xD0` | `LIST_SAVED` / `SAVED_LIST` | 双向 | 已存信号清单(元素结构见 §9.1) |
| `0x51` | `DELETE_SAVED` | App→设备 | `{name}` |
| `0x52` / `0xD2` | `GET_STATUS` / `STATUS` | 双向 | 温度、供电、Wi-Fi RSSI、模块状态(字段名见 §9.1) |
| `0x53` | `SET_NET` | App→设备 | `{ssid, pass}` — 现场配网(⏸ 本批无 Wi-Fi,不实现) |
| `0x54` | `OTA_BEGIN` | App→设备 | 预留,见 §9 非目标 |

## 4. 能力集 `CAPS`
App **必须**以 `CAPS` 为唯一真实来源,不得硬编码频段。

```json
{ "proto": 1, "fw": "0.1.0", "device_id": "RFLAB-A1B2", "active_slot": 1,
  "transports": ["usb", "ble", "wifi"], "active_transport": "usb",
  "modules": [
    { "slot": 1, "chip": "si4463", "present": true, "tx_max_dbm": 20, "tx_allowed": false, "bands": [[425000000, 525000000]] },
    { "slot": 2, "chip": "cc1101", "present": true, "tx_max_dbm": 10, "tx_allowed": false, "bands": [[387000000, 464000000]] },
    { "slot": 3, "chip": "nrf24l01p", "present": true, "tx_max_dbm": 20, "tx_allowed": false, "bands": [[2400000000, 2525000000]] } ],
  "policy": { "tx_whitelist": [], "tx_power_cap_dbm": 20, "tx_requires_token": true },
  "limits": { "ble_frag_total_max": 15, "ble_frame_max": 3600, "usb_frame_max": 16384, "max_edges": 16384, "stream_rate_hz": 50, "arm_timeout_ms": 5000 } }
```
> ⚠️ 上面示例里的 `transports: ["usb", "ble", "wifi"]` 列出的是**协议已定义**的三条链路;BLE/Wi-Fi 本批 ⏸ 移出(§2.2),**固件实际只上报 `["usb"]`**,`active_transport` 也只可能是 `"usb"`。示例保留 `ble_*` limits 是为了后续批次直接接续,不代表本批可用。
>
> ⚠️ **`modules[].bands` 必须是模块级、不是芯片级**。误用芯片能力(如把 SI4463 写成 142–1050MHz、CC1101 写成三段)会让 App 以为能扫到实际扫不到的频段——这正是"带外频段必须可见地失败"要禁止的失效模式。真实值:**M1 425–525MHz、M2 387–464MHz、M3 2400–2525MHz**;固件**必须**按实际装的前端上报。
`policy.tx_whitelist` 为空 = **禁止一切发射**,出厂默认即为空。**`policy.tx_power_cap_dbm` 出厂 20**:产品在屏蔽室内使用,**所有模块按各自模块上限满功率出厂**。该字段是**设备级全局最大值**,**不是"每模块的值"、也不是"按频段的表"**;某次发射的**有效上限 = min(请求值, 本字段, 该模块 `tx_max_dbm`)**。按当前硬件 `min(20, module)` 得 M1 = 20、**M2 = 10**、M3 = 20——**M2 达不到 20 是模块硬件所致,不是被策略限住**,该区别必须体现在 UI 文案上。功率门只防"超过模块/硬件能力",**不再承担法规限幅职责**;真正的约束落在**频段白名单**与**显式用户确认**。`power_dbm` **必须**可由用户在 App 内调节:出厂默认满功率,但设备**不得**忽略 `TX_REQUEST.power_dbm` 而恒用上限;设备回报的 `granted_power_dbm` 与请求值同时存在时以 `granted_power_dbm` 为准。
**`transports` 与 `active_transport`**:设备支持的链路清单与当前承载会话的那一条(§2.2 活动链路唯一性)。`active_transport` 在链路切换时变化,App 应以 `HELLO_ACK` / `STATUS` 的最新值为准。
`limits` 各项:

| 字段 | 含义 |
|---|---|
| `ble_frag_total_max` | BLE 分片上界,固定 `15`(4bit `FRAG_TOTAL`) |
| `ble_frame_max` | **当前 BLE 连接下单帧最大 `LEN`**(按实际协商的 ATT 载荷算出,§2.2.1)。App 必须读它定请求规模;Wi-Fi 下此值不代表 Wi-Fi 上限 |
| `usb_frame_max` | **USB 链路上单帧最大 `LEN`**(§2.2.2)。USB 无 BLE 分片约束故可比 BLE 大得多,但**仍须**用它约束 `DATA_CHUNK` 分块 |
| `max_edges` | 单次捕获跳变数硬上限;请求值超出即**钳制**并回 `WARN{edge_clamped}`(§3.4) |
| `stream_rate_hz` | 高频消息令牌桶速率(§2.4) |
| `arm_timeout_ms` | `TX_STATUS{state:"armed"}` 的等待窗口(§3.7);超时未收到二次确认则回落 `failed`/`arm_timeout`。**必须有承载,否则 App 无法知道设备的实际窗口** |

## 5. 状态机

```text
IDLE ──HELLO/HELLO_ACK──► READY ──SELECT_MODULE──► MODULE READY
                              ▲                         │ START_SCAN / CAPTURE_START
                              │                         ▼
                              └── ABORT / ERROR ◄── SCAN/CAPTURING ──DONE/ABORT/TIMEOUT──► SCAN_DONE
```
规则:
1. `IDLE` 下除 `HELLO` 外一切命令回 `ERROR{not_ready}`;
2. `SCAN` 与 `CAPTURE` 互斥;`CAPTURE_START` 在扫描中会先隐式停扫;
3. 发起新任务会**隐式中止**进行中的任务(不是排队),并发 `WARN{preempted}`;
4. 任何状态收到 `ABORT` 均回 `IDLE/READY`,并保证射频进入 idle。

## 6. 错误码

| code | 含义 | App 应对 |
|---|---|---|
| `not_ready` | 未完成 `HELLO` | 重握手 |
| `version_mismatch` | 协议版本不符 | 提示升级 |
| `module_absent` | 目标模块未检测到 | 重新 `LIST_MODULES` |
| `module_busy` | 模块被占用 | 重试或先中止 |
| `band_out_of_range` | 频率超出模块能力 | 用 `CAPS.bands` 夹取 |
| `band_clipped`(WARN) | 部分频段被裁剪 | 提示真实覆盖 |
| `tx_denied` | 违反发射策略 | 展示原因,不得重试 |
| `tx_bad_token` | 授权令牌无效(实为 `seq` 失步) | 按 §7.1(3.1) 重新对齐基线 |
| `frag_timeout` | 分片重组超时 | 重发请求 |
| `frame_too_large` | 帧超出当前承载上限(如 BLE 上 `LEN > ble_frame_max`) | 降低请求规模后重发;不得重试同尺寸 |
| `arm_timeout` | 已 `armed` 但未在窗口内收到二次确认,自动回落 | 重新走一次两段式发射(§3.7) |
| `throttled`(WARN) | 限流丢数据 | 降采样或缩短捕获 |
| `preempted`(WARN) | 进行中的任务被新任务**隐式中止**(§5 规则 3) | 无需处理;若 App 正处于该任务的 UI 流程中,应把结果标为"被抢占"而非"失败" |
| `edge_clamped`(WARN) | `max_edges` 请求值超上限被钳制 | 读 `CAPS.limits.max_edges` 后重发 |
| `hw_error` | SPI/RMT 异常 | 报错并建议重启模块 |
| `unsupported` | 未知消息类型 | 忽略 |
> `no_display` / `bad_code` / `ble_only` 是配对错误码(§7.1(1.1)),不是 `ERROR.code`,不进本表。

## 7. 安全

| 面 | 要求 |
|---|---|
| 控制通道 | 本批唯一通道为 **USB CDC-ACM**(物理在场即天然可信)。⏸ Wi-Fi(`wss://`,自签证书 + 指纹校验,或局域网信任模式;默认仅监听局域网)与 BLE 本批不实现;注意**浏览器没有证书指纹固定 API**,`wss://` 指纹方案只有原生外壳能实现 |
| 配网 | ⏸ 本批无 Wi-Fi,`SET_NET` 无实现路径。恢复 Wi-Fi 时:首次配网**必须经 USB**(唯一可信配对信道),**不经明文广播密码**,凭据写入沿用带内 `SET_NET`;设备当前 IP/SSID 尚无承载字段(§9.1),实现前不得宣称该承诺已兑现 |
| 设备身份 | `HELLO_ACK.device_id` + 首次配对绑定的长期密钥,防串台 |
| 发射授权 | `tx_requires_token` 为真时,`TX_REQUEST` 必须带 App 计算的授权令牌(HMAC-SHA256,一次性)。**令牌没有"有效期"概念**——防重放靠单调 `seq` 而非时间,因为设备无 RTC。完整定义见 §7.1 |
| 审计 | 每次 `TX_REQUEST`(含被拒的)写本地环形日志:时间、频率、功率、结果 |
| 密钥 | **DeepSeek API Key 绝不进设备** |

### 7.1 发射授权令牌 `auth_token` 的可实现定义
目标是"**离线可用 + 防重放 + 抗串台**",不引入云端依赖。

**(1) 长期密钥的建立(首次配对,本批只走 USB)**

```text
设备开机若未配对:生成 32B 随机 pairing_secret(硬件 RNG)
  → 存 NVS(不可读回,只可被校验逻辑调用);同时生成 6 位一次性配对码 pair_code
App   → HELLO{client_id, pairing:true}                    # USB 链路:不要求 pair_code
设备  → HELLO_ACK{pair_ok:true, first_bond:true, pairing_secret:"<32B base64url>"}   # 仅 USB
   或  HELLO_ACK{pair_ok:false, pair_error:"<见 (1.1)>"}
```
- `pair_code` 的作用是**防中间人**:若配对请求被截获,攻击者拿不到设备端的一次性码。`pairing_secret` 只经**可信链路 USB** 下发一次(要求操作者物理在场),**Wi-Fi 与 BLE 路径都不下发**,只用于之后的令牌校验。
- ⏸ **`pair_code` 本批无搬运通道**:显示载体(OLED)延期到下一批。USB 是**物理端口**,连接行为本身已证明"操作者在场",故 **USB 配对不校验 `pair_code`,不产生 `bad_code`**;BLE 链路上它仍是必要条件,而本批没有码的搬运通道 ⇒ **BLE 配对一律回 `pair_error:"no_display"`**,不得静默降级为免码配对(那会让防中间人机制名存实亡)。本批 BLE 整条移出,`no_display` 与 `bad_code` 均为**无活路径的保留值**:不要为它们实现分支或造用例,也不要删除。
- 一台设备只与一个 `client_id` 配对(主绑定);改绑需**设备端物理按键确认**。⚠️ USB 的在场性防不了"别人趁你不在插线改绑"。
- **`pairing_secret` 的承载** = `HELLO_ACK` 的扩展字段(仅在 USB 上出现),**不新增消息类型**:配对是握手的一部分,拆成独立消息会引入"半配对"中间态。⚠️ **只搬运配对码,绝不搬运 `pairing_secret` 或任何可用于发射的凭据**——人眼可读通道(屏幕、标签、灯光)是**带外信道**,经它传递的密钥意味着不在设备旁边的人也能取得(拍照/录屏),会破坏"发射必须在场"的红线。
**(1.1) 配对失败的表达(`pair_error`)**

| `pair_error` | 含义 | App 应如何引导 |
|---|---|---|
| `bad_code` | `pair_code` 不匹配 | ⏸ **本批为保留值,无活路径**(USB 不校验码;BLE 在取到码之前即回 `no_display`)。下一批 OLED 到位后启用 |
| `no_display` | 设备当前没有 `pair_code` 的搬运通道(本批 BLE 上必然如此) | **不得**提示"重试";应提示改用 **USB 有线连接完成首次配对**,或说明本设备需加装显示模块 |
| `already_bound` | 设备已绑定其他 `client_id` | 提示需在**设备端按键**才能改绑,而不是反复重试 |
| `ble_only` | 在 Wi-Fi 链路上尝试配对 | ⏸ **本批无活路径**(Wi-Fi 已移出);后续若恢复,语义为"**改用 USB**"。不要实现或造用例 |
| `rng_failed` | 设备侧 RNG 初始化失败 | **拒绝配对**并报错;不得降级为弱随机 |
**(1.2) `seq` 基线与失步恢复(防永久锁死)**
设备**应**在 `HELLO_ACK.last_accepted_seq` 中回传自己已接受的最大 `seq`(**无符号整数,未发射过任何信号时为 `0`**)。App 每次成功 `HELLO` 后,把本地计数器置为 `max(本地值, last_accepted_seq)` 再继续递增;**为什么必须**:若 App 清空数据/重装/换机,本地 `seq` 从 0 重新开始,而设备要求 `seq` **严格大于**已接受最大值 ⇒ 会**永久拒绝一切发射**,且错误码只有 `tx_bad_token`,用户无法自救。**重新配对会重置该基线**:新 `pairing_secret` 建立时,设备同时把 `last_accepted_seq` 归零(旧令牌随之全部失效,这是期望行为)。**`tx_bad_token` 的恢复路径必须闭环**(见 (3.1)),**不得无限重试**。
**(2) 令牌计算(两端同式)**

```text
token = HMAC-SHA256(key = pairing_secret,
                    msg = device_id | seq_be32 | f_hz_be64 | mod | power_dbm_x10_be16)
token_wire = base64url(token[0:16])          # 截断到 128bit,足够
```
`seq` 是 App 单调递增计数器(每设备独立),设备记录**已接受的最大 seq**;`device_id` 绑定设备身份,防令牌跨设备重放;频率/功率/调制绑定到**这一次具体发射**,令牌不能挪用于改频或加功率。
**(3) 校验规则(设备侧,任一不通过即 `ERROR{tx_bad_token}`)**:① 防重放:`seq` 必须**严格大于**已接受的最大值;② 参数绑定:重算 HMAC,用**常数时间比较**(防时序侧信道);③ 令牌**一次有效**,校验通过后立即记录 `seq`;④ **不依赖任何绝对时钟**:设备只用 `uptime_ms` 做日志与相对时间戳,无法也不应判断"令牌是多少秒前签发的"。
> ⚠️ **实现陷阱**:若固件按"令牌 30 秒过期"实现,在无 RTC 的设备上会表现为**恒拒**(时间从未前进到签发时刻)或**恒过**(时间复位后旧令牌都"新鲜"),**两者都是缺陷**。正确实现里不存在"过期"这个概念,只有 `seq` 单调性。
**(3.1) `tx_bad_token` 的恢复闭环**
`tx_bad_token` 是**可恢复**错误,但恢复动作不是重试,而是重新对齐基线:

```text
收到 tx_bad_token
  → 重新 HELLO,读 HELLO_ACK.last_accepted_seq
  → 本地 seq := max(本地 seq, last_accepted_seq) + 1
  → 重签令牌再试一次(仅一次)
  → 仍失败 → 提示「设备可能已与其它手机配对,请在设备端按键后重新配对」
```
**(4) 与"四道门禁"的关系**:`auth_token` 只是**第四道**(授权)门。前三道——频段白名单、功率上限、显式用户确认——**任何一道不过即拒绝,且不因令牌有效而放宽**(实现见 [03-firmware.md](03-firmware.md))。
**(5) 一条必须显式承认的结论:只经 Wi-Fi 且不接线的设备无法发射**
`pairing_secret` 只经可信链路(USB)下发,因此在**只有 Wi-Fi、且不插线**的链路上:设备永远拿不到 `pairing_secret` ⇒ 无法完成绑定 ⇒ `tx_requires_token` 永远无法满足 ⇒ **该设备永久不可发射**。这不是 bug,是"发射默认拒绝 + 密钥必须经带外可信信道下发"两条原则的直接推论。
> ⏸ **本批无对象**(USB 既是唯一链路也是唯一配对信道,"能连上就一定能配对"),但结论**不是失效而是换了更硬的形态**:原失效模式是"有链路但绑定不了",本批的对应风险是"**连链路都没有**"(WebUSB 不可用 ⇒ 既不能配对也不能控制,后者更糟)。**不要因本批没有 Wi-Fi 就删掉本节**——后续批次恢复 Wi-Fi/BLE 时它逐字恢复效力,而这正是"为什么必须有一个物理可信信道"的论证。曾登记的处置选项(设备端本地授权 / 带外传递密钥)均未采纳,若改选须重新评估"远程不可发射"红线。

## 8. 版本演进
- `VER` 只在**帧格式**变化时升。**新增消息类型 / 新增字段不升版本**;移除字段前必须先在该字段所属能力集的 `CAPS.deprecated:[...]` 中标记,保留至少一个小版本;
- App 声明 `proto_min/proto_max`,设备取交集;无交集即 `ERROR{version_mismatch}`;
- **字段适配规则**:设备与 App 必须**按字段而不是按版本号**适配。v1.1 新增了消息类型与字段(`GET_SCAN_HITS`/`SCAN_HITS`、`features` 的置信度组、配对字段、`granted_power_dbm`)但**未改帧格式**,故 `VER` 仍为 `0x01`;其中 `SCAN_DONE.hits → hits_total` 属**破坏性改名**,因本项目**尚无任何已发布固件/App**,直接改名而不留兼容层;若将来 v1 固件已流出,必须走 `deprecated` 流程。

## 9. 非目标
- ❌ 不做 OTA 固件升级(§3.8 `OTA_BEGIN` 仅占位);❌ 不做设备间互联 / Mesh;
- ❌ 不做云端中转;Wi-Fi 场景是**同网段直连**;
- ❌ 不传原始 IQ 采样(带宽不允许),只传跳变序列 + 特征;
- ❌ **不做设备侧离线记录 / 回读**:该能力的前提是"无人值守远程部署",而该场景已取消 ⇒ 不存在"App 不在场"的时段。**不新增**"记录会话 → 回读"消息对;若将来重开无人值守,须先改产品定义再回来加协议。

### 9.1 已知未定义 / 显式推迟到 v1.2 的项
以下**不是遗漏,而是有意延后**——写在这里是为了让固件/App/schema 三方都知道"现在是不知道,不是随便填":

| 项 | 现状 | 为什么推迟 |
|---|---|---|
| `STATUS` 的字段名 | §3.8 只写"温度、供电、Wi-Fi RSSI、模块状态",无字段名 | 需先定硬件采样点与单位(待实测);现在编字段名会与实测口径打架 |
| `SET_NET` 的结果回报 | 缺 `NET_STATUS{ok,ip,ssid,err}` ⇒ App 只能轮询猜配网成败;设备 IP 无法读取 | 可在 v1.2 与 `STATUS` 一起定义,避免两次改动 |
| 读取设备侧审计 | 缺 `GET_AUDIT`/`AUDIT_LOG` ⇒ App 审计镜像无法闭环 | 需先定环形日志容量与分页口径 |
| `CAPS.device_name` / `CAPS.bound` | 缺;App 无法显示设备名与绑定状态 | UI 必需但非阻塞;v1.2 与 `regulatory_region` 一并加 |
| `CAPS.regulatory_region` | 缺;App 无法判断设备加载的是哪套白名单常量集 | 依赖目标使用地定案;未定前加了也是错的 |
| `SAVED_LIST` / `LIST_SAVED` 的元素结构 | 只写"已存信号清单" | 与 `SAVE_SIGNAL` 是否复用同一结构未定 |
| `PONG.t_ms` 语义 | 未写是回显值还是设备本地时刻 | 影响 RTT 计算精度;v1.2 明确 |
> `CAPS.deprecated` 本身的形状(字符串数组)与 §8 的用法一致,当前无弃用字段故不出现在例示中。
