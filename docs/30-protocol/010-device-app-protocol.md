# RF-Lab 设备 ↔ App 协议契约

状态:`draft` · 版本:`v1.1` · 最后更新:2026-09-21

> **v1.1 变更(协调者修订,2026-09-20)**:固件/App/测试/schema 四份产出**独立提出**了同一批契约缺口,本版一次性关闭。对照关系:
>
> | 变更 | 关闭了谁的反馈 |
> |---|---|
> | §2.2.1 新增:BLE 帧上限推导(`LEN` 4096 与 15 分片**数学上不相容**,差 427B+)与 `CAPS.limits.ble_frame_max` | 固件 F-Q9、App §2.1、测试 |
> | §2.4/§3.3 限流与覆盖诚实性:新增 `GET_SCAN_HITS`/`SCAN_HITS`,`SCAN_DONE` 拆 `hits_total`/`hits_pushed`/`hits_dropped` + `scan_id` | 固件 F-Q7、App §9-8 |
> | §3.4 `max_edges` **钳制 + `truncated` + `WARN{edge_clamped}`**;§3.7 `TX_STATUS.armed` 两段式语义 + `arm_timeout` | 固件 F-Q8、App §9-6 |
> | §3.5 `FLAGS` bit1 判决点;§3.6 `raw_semantics`(三值) | 固件 F-Q6、App §9 |
> | §3.6 `features` 三级不确定性:`*_confidence`(可 `null`)、猜测字段可 `null`、`incomplete`、**字段缺失降级规则** | 固件 F-Q5、App §9-2、AI §10-1 |
> | §3.2 `spi` 语义(单总线 + 3 CS,`(bus,cs)` 唯一标识) | 固件 F-Q8 附带、硬件 §10 |
> | §3.7 `mod` 限定 `ook`/`2fsk`(不含 `auto`)+ `payload_rle` 两方向统一编码 | schema §7-2/3、App §9-9、AI §10-2 |
> | §6 新增 `frame_too_large` / `arm_timeout`,`preempted` 补登为 WARN;**`module_busy` 与抢占的边界** | 测试 V-01/V-02 |
> | §7.1 新增:`auth_token` 可实现定义(HMAC-SHA256 + `seq` 单调性,**不用时间窗**——设备无 RTC)、`HELLO_ACK.last_accepted_seq` **死锁恢复**、`pair_error` 失败表达、`pairing_secret` 承载与**可信链路限定**、以及**Wi-Fi-only 设备永久无法发射**的显式结论 | App §9-3/4/§8.2、AI §10-3、schema §7-7 |
> | §3.6.1 时间基准(`t_ms` = uptime,**非**绝对时间) | App §9-10 |
> | §9.1 新增:显式登记已知未定义项与 v1.2 推迟清单 | schema §7、App §9 |
>
> **v1.1 追加变更(2026-09-21,用户决策 NQ-01/NQ-03)**:不升版本,仅修语义与补一处真实缺口——**① §7.1(1) 定稿本批配对只走 USB**(`pair_code` 在 USB 上不作必要条件;BLE 配对一律回 `pair_error:"no_display"`;`bad_code` 本批为无活路径的保留值);**② §4 功率口径改为"所有模块各自满功率出厂,且可由 App 调节"**(C-11 修订 + C-12):`policy.tx_power_cap_dbm` 是**设备级全局最大值**,有效上限 = `min(请求, 全局 cap, 模块 tx_max_dbm)`,M2 的 10dBm 是模块硬件所致而非策略限幅;**③ §3.7 新增 `TX_STATUS.granted_power_dbm`**(可选字段)——原契约要求审计记"实际生效功率"却没有承载字段,而 schema 是 `additionalProperties:false`,**固件照契约实现反而回报不了**,属真实缺口。**帧格式未变**;`TX_STATUS` 仅新增一个可选字段(向后兼容)。
>
> `protocol/schemas/` 已同步(`$defs` 29 个,`$ref` 70 处),自检 `python3 tools/validate_schemas.py` 退出码 0。

> **本文件是契约的权威来源。** 固件、App、AI 提示词三者都以此为准;任何字段改动必须先改本文件并升版本号。
> 机器可读版本见 [`protocol/schemas/`](../../protocol/schemas/),校验工具 `python3 tools/validate_schemas.py`。

---

## 1. 设计原则

| 原则 | 理由 |
|---|---|
| **一份帧格式,三条传输** | Wi-Fi(WebSocket)、BLE 与 **USB** 共用同一套消息语义,避免多套逻辑分叉 |
| **控制用 JSON,数据用二进制** | 控制消息可读、可调试;捕获数据体积大,必须走二进制分片,不走 base64+JSON |
| **设备不持有密钥** | 设备只回传特征;DeepSeek API Key 仅存于手机(App 侧决策) |
| **发射默认拒绝** | 一切发射需频段白名单 + 功率上限 + 显式授权,软件强制而非文档约定 |
| **能力协商优先** | App 启动先读能力集,不假设模块存在;多模块选型由用户配置驱动 |
| **前向兼容** | 未知字段忽略,未知消息类型回 `ERROR{unsupported}`,不崩溃 |

## 2. 帧格式

三条传输共用同一帧头(小端),**头后内容按通道类型解析**:

```
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
| `LEN` | 2B LE | `PAYLOAD` 字节长度。**格式层逻辑上限 4096**,但**实际可达上限由承载决定,见 §2.2.1**;Wi-Fi 上限 65535 |

### 2.1 PAYLOAD 解析规则

| 帧类型 | 解析方式 |
|---|---|
| 控制帧(`TYPE` 最高位 = 0) | UTF-8 JSON 文本 |
| 数据帧(`TYPE` 最高位 = 1) | §3.4 定义的二进制块 + 尾部 JSON 元数据 |

### 2.2 传输映射

**三条传输共用同一套帧格式与全部消息语义。** 这是本契约的核心约定:新增一条传输**不新增任何消息类型**。

| 通道 | 承载 | 参数 |
|---|---|---|
| **Wi-Fi** | WebSocket 二进制帧(1 帧 = 1 个 RF-Lab 帧) | 设备开 AP 或连现场路由器;`ws://<ip>:8266/v1` |
| **BLE** | GATT Notify / Write,自定义 Service | 单帧超 MTU 时按 §2.3 分片;可达上限见 §2.2.1 |
| **USB**(ADR-0003 / F-11) | **CDC-ACM 字节流**(手机为 USB 主机,设备为从设备) | 复用 ESP32-S3 内建 **USB Serial/JTAG**;上限见 §2.2.2 |

**活动链路唯一性**:同一时刻**只有一条**链路承载会话。新链路完成 `HELLO` 时,旧链路被隐式中止(按 §5 规则 3,旧链路收到 `WARN{preempted}`)。**禁止**两条链路同时下发命令——否则 `seq` 防重放校验与状态机抢占都会出现竞态。

> ⚠️ **一条必须实现的优先级例外**:`USB` **可以抢占**正在使用的 Wi-Fi/BLE 链路。理由:USB 的使用场景正是"无线已不可用或即将不可用"(2.4GHz 模块以 20dBm 发射时,本机 Wi-Fi/BLE 会被自己的 PA 压掉,见硬件文档 §4.2)。**若 USB 不能抢占,就会出现"最需要中止的时候恰恰接管不了"的死结**——这正是判据 A-07 要防的失效模式。

### 2.2.2 USB 上"一个完整帧"能有多大

USB CDC 是**包边界明确的字节流**,没有 BLE 那样的 20–244B 分片约束,因此:

- **不需要 `FRAG` 头**(§2.3 的分片机制**只适用于 BLE**)
- 上限取 `CAPS.limits.usb_frame_max`,设备按自身缓冲给出
- 虽无协议层小上限,**仍不得**把整次捕获塞进一帧:捕获数据必须按 `DATA_CHUNK` 分块(块大小 ≤ `usb_frame_max`),以便 App 增量展现进度、且任一侧缓冲不会爆
- **禁止静默截断**:超限一律回 `ERROR{frame_too_large}`(与 BLE 同规)

### 2.2.1 BLE 上"一个完整帧"到底能有多大(**先算清楚,不要拍脑袋**)

`LEN` 的字段类型允许到 4096,但**字段能表示 ≠ 能传过去**。整帧必须被切成 ≤15 个分片(§2.3 的 4bit `FRAG_TOTAL`),而每片能装多少由协商出来的 ATT 载荷决定。逐项推导(取 BLE 5.0 数据长度扩展后的典型值):

```
LL_Payload(251) − L2CAP 头(4)                        = ATT_Payload 247
每片占用 = ATT 操作码(1) + FRAG 头(1)                = 2
每片可装 = 247 − 2                                    = 245B
可重组整帧上限 = 245 × 15                             = 3675B
LEN 上限 = 3675 − 帧头 6                              = 3669B
```

**结论:BLE 上 `LEN > 3669` 的帧在数学上无法完整分片传输**(契约原写 4096,**超出 427B**)。且 `3669` 是**上界不是目标**——实际 ATT 载荷由协商决定(常见还要小,如未启用数据长度扩展时 ATT 载荷仅 23B),协商越小可重组整帧越短。

> **推导口径备忘**(避免再次算错):`247` 是 **ATT 载荷**(= LL 载荷 251 − L2CAP 头 4)。每片还要各自扣掉 **ATT 操作码 1B**(Write Command / Notification)与 **FRAG 头 1B**,故每片净载 **245B**。`245 × 15 = 3675` 是**整帧**(含 6B 帧头)上限,减去帧头 6B 才得到 `LEN` 上限 `3669`。**不要**在 245 里再减一次帧头——帧头全帧只出现一次,不在每片里。

因此规定:

- **`CAPS.limits.ble_frame_max` 是唯一真实来源**(设备按实际协商的 ATT 载荷算出来并上报),App **必须**用它决定单次捕获/发射请求的规模,不得硬编码 4096 或 3669
- **保守设计建议:BLE 路径上单帧 `LEN` ≤ 3600**(在 3669 上界内留充分余量,足以吸收 MTU 协商差异);Wi-Fi 路径可达 65535
- 设备收到**已知超出当前承载上限**的帧,回 `ERROR{frame_too_large}` 并给出上限值。**禁止**静默截断——截断的捕获数据会被下游误当作完整信号
- 因此 `DATA_CHUNK` 的**分块上限是承载相关的**:同一段捕获在 Wi-Fi 上可以少分几块,在 BLE 上必须多分。设备侧按 `ble_frame_max` 自适应选块大小,App 侧不得假设固定块大小

> 推导依据:Bluetooth Core 规范 L2CAP 头为 4B(2B 长度 + 2B CID),BLE 5.0 数据长度扩展后 LL 载荷上限 251B,故 ATT 载荷 247B。旧文档/旧代码里常见的 "MTU 244" 与 "MTU 247" 是不同层级的口径,**本契约统一采用 247(ATT 层级)**。

### 2.3 分片(BLE 必需)

BLE ATT MTU 协商后有效载荷通常 20–244B。分片规则:

```
FRAG 头 (1B): [FRAG_IDX:4bit | FRAG_TOTAL:4bit]  -- 总片数上限 15
```

> **`FRAG 头` 是 1 字节,不是 2 字节**(两处旧表述不一致,此处为准)。两个字段各占 4bit,恰好塞满 1 字节:`FRAG_IDX` 取 0–15,`FRAG_TOTAL` 取 1–15。任何按"2 字节头"实现的固件都会与 App 侧错位。

- `FRAG_TOTAL = 1` 时省略 FRAG 头,直接发原帧(兼容 Wi-Fi 路径)
- 接收端按 `SEQ + FRAG_IDX` 重组;缺片超时 500ms 即丢弃整帧并回 `ERROR{frag_timeout}`

### 2.4 分片之外的流控

- 设备→App 的 `SCAN_HIT` 与 `DATA_CHUNK` 是高频消息,**必须**受令牌桶限流(默认 50 msg/s)
- 超限时设备丢**旧**数据保**新**数据,并发 `WARN{throttled}` 计数

> ⚠️ **限流不得吃掉"覆盖诚实性"。** 用 25kHz 步进扫 779–928MHz 全带有 5960 个点,低阈值下命中可达数千条,按 50 msg/s 根本发不完——若此时悄悄丢命中,App 会以为"这段频谱很干净",而这是**危险的错误结论**。因此 §3.3 的 `SCAN_DONE` 必须携带完整的命中统计与丢弃计数,**限流只影响实时推送的及时性,不影响结果的完整性声明**。需要全部命中明细时,用 `0x23 GET_SCAN_HITS` 分页取回。

## 3. 消息类型

### 3.1 会话与能力

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x01` | `HELLO` | App→设备 | `{app_version, proto_min, proto_max, client_id}`;**配对时另带** `{pair_code, pairing:true}`(见 §7.1) |
| `0x81` | `HELLO_ACK` | 设备→App | `{fw_version, proto, device_id, chip:"esp32s3", uptime_ms, last_accepted_seq}`;配对相关字段见 §7.1 |
| `0x02` | `GET_CAPS` | App→设备 | `{}` |
| `0x82` | `CAPS` | 设备→App | 见 §4 |
| `0x03` | `PING` / `0x83 PONG` | 双向 | `{t_ms}` 用于 RTT 与保活(默认 5s) |
| `0x7F` | `ERROR` | 设备→App | `{code, msg, seq_ref}` |
| `0x7E` | `WARN` | 设备→App | `{code, msg}` |

### 3.2 模块管理

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x10` | `LIST_MODULES` | App→设备 | `{}` |
| `0x90` | `MODULES` | 设备→App | `[{slot, chip, present, bands, tx_max_dbm, driver_version, spi:{bus,miso,mosi,sck,cs}, ant:"SMA"}]` — `spi` 各字段含义见下方说明 |
| `0x11` | `SELECT_MODULE` | App→设备 | `{slot}` — 切换当前活动模块 |
| `0x91` | `MODULE_STATE` | 设备→App | `{slot, state:"ready"\|"busy"\|"absent"\|"error", detail}` |

> **约束**:同一时刻只有一个**活动**模块可收发。切换模块会 `STOP_SCAN` / `ABORT_CAPTURE`,并丢弃未完成的捕获。

**`spi` 字段语义**(硬件最终采用"单 SPI2 总线 + 每模块独立 CS",见硬件架构文档 §3 方案 B,与约束 C-01「每模块独立 SPI」的关系见该文档):

| 字段 | 含义 | 备注 |
|---|---|---|
| `bus` | 主机 SPI 控制器序号 | **本项目恒为 `2`**(SPI2_HOST) |
| `miso` / `mosi` / `sck` | 该总线的引脚号 | **三个模块上报值相同**——它们物理上共用同一组线,这是正常且预期的 |
| `cs` | 该模块的片选引脚 | **三模块各不相同**,是唯一区分项 |

> 即:`(bus, cs)` 联合唯一标识一个模块;`bus` 相同**不代表**模块冲突,`cs` 相同才是配置错误。App 不应把"三模块 `bus` 相同"当作异常报警。

### 3.3 扫频与 RSSI

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x20` | `START_SCAN` | App→设备 | `{f_start_hz, f_stop_hz, step_hz, dwell_ms, rssi_threshold_dbm, mode:"peak"\|"sweep"}` |
| `0xA0` | `SCAN_HIT` | 设备→App | `{scan_id, f_hz, rssi_dbm, t_ms}` — 仅回超阈值的点。`t_ms` 是设备**单调 uptime**(ms),**不是绝对时间**(见 §3.6.1 时间基准) |
| `0xA1` | `SCAN_PROGRESS` | 设备→App | `{f_cur_hz, pct, elapsed_ms, hits}` |
| `0x21` | `STOP_SCAN` | App→设备 | `{}` |
| `0xA2` | `SCAN_DONE` | 设备→App | `{hits_total, hits_pushed, hits_dropped, elapsed_ms, aborted:bool, coverage:[{f_start_hz,f_stop_hz}...]}` |
| `0x23` | `GET_SCAN_HITS` | App→设备 | `{scan_id, offset, limit}` — **分页取回**上次扫描的完整命中明细 |
| `0xA4` | `SCAN_HITS` | 设备→App | `{scan_id, total, offset, hits:[{f_hz,rssi_dbm,t_ms}...]}` |
| `0x22` | `SET_RSSI_STREAM` | App→设备 | `{f_hz, interval_ms, enable}` — 定点实时 RSSI |
| `0xA3` | `RSSI_SAMPLE` | 设备→App | `{f_hz, rssi_dbm, t_ms}` — `t_ms` 同 `SCAN_HIT`,为设备单调 uptime |

**`SCAN_DONE` 计数语义**(三者必须自洽,否则属设备实现缺陷):

| 字段 | 含义 |
|---|---|
| `hits_total` | 本次扫描**实际检测到**的超阈值点数,一个不落 |
| `hits_pushed` | 因限流与分页而**已通过 `SCAN_HIT` 推送给 App** 的条数 |
| `hits_dropped` | `hits_total − hits_pushed`,即需要在限流窗口内被丢弃的条数 |

- 只需**结论**时:App 读 `hits_total` 与 `hits_dropped` 即可,无需取明细
- 需要**明细**时:用 `GET_SCAN_HITS` 按 `scan_id` 分页拉取;命中缓存在设备端保留到**下一次扫描开始**
- **禁止**把 `hits_dropped > 0` 当成正常结束而不告知用户——App 发现该值非零时必须提示"本次明细不完整",避免把"没推送"误读为"没信号"

> **覆盖诚实性**:`SCAN_DONE.coverage` 必须回传**真实扫过的频段**。若请求范围超出模块能力,设备不得静默跳过——必须回 `WARN{band_clipped}` 并给出实际覆盖区间。这是 ADR-0001 的直接落地要求。

### 3.4 捕获与原始数据

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x30` | `CAPTURE_START` | App→设备 | `{f_hz, mod:"ook"\|"2fsk"\|"auto", max_edges, timeout_ms, trigger_rssi_dbm}` |
| `0xB0` | `DATA_CHUNK` | 设备→App | **二进制帧**,见 §3.5 |
| `0xB1` | `CAPTURE_DONE` | 设备→App | `{edges, duration_ms, truncated:bool, features:{...}}` |
| `0x31` | `CAPTURE_ABORT` | App→设备 | `{}` |

**`max_edges` 冲突语义(必须明确,否则两端行为不可预测)**:当 `CAPTURE_START.max_edges` 大于 `CAPS.limits.max_edges` 时,设备**钳制到后者并继续捕获**,同时发 `WARN{edge_clamped}`;`CAPTURE_DONE.truncated` 置 `true`。**不得报错拒绝**——报错会让 App 无法用"尽量多采"的意图工作,而钳制是信息无损的降级(截断事实已由 `truncated` 显式表达)。

> 三层防线的一致性:契约钳制值 = `CAPS.limits.max_edges` = 固件编译期硬上限,三者必须同源(见固件架构文档 §5)。App 应先读 `CAPS` 再决定请求值,把钳制当作异常路径而非正常路径。

### 3.5 二进制载荷布局(`TYPE` 最高位 = 1)

原始捕获的价值在**时序**,所以载荷是变长 RLE,而不是采样数组:

```
+--------+------------------+---------------------------+
| FLAGS  | edge[0] 起点     | RLE 对 (dt_us, level) ... |
| 1B     | u32 LE 相对微秒  | 每对: u16 LE dt | u8 level |
+--------+------------------+---------------------------+
尾部: 0x00 0x00 后接 UTF-8 JSON 元数据
```

| 项 | 说明 |
|---|---|
| `FLAGS` bit0 | 载荷语义:`0`=OOK/ASK 电平序列,`1`=FSK 过零/相位序列 |
| `FLAGS` bit1 | **判决点**:`0`=**未经判决**的模拟前端数字化序列(比较器/过零整形输出,**保留时序**),`1`=**芯片内部已判决**的位流(时钟已恢复,时序信息已丢失) |
| `dt_us` | 与上一跳变的间隔,微秒。饱和值 `0xFFFF` 表示"超长间隔",真实值在元数据 `long_gaps[]` 里 |
| `level` | `0`/`1` |
| 元数据 JSON | `{f_hz, mod, rssi_dbm, sample_clock_hz, long_gaps:[...]}` |

> **为什么 bit1 必须存在**:同是"FSK 数据",`bit0=1,bit1=0` 是**过零序列**(跳变间隔 = 载波周期级,可反推频偏),`bit0=1,bit1=1` 是**芯片解调后的位流**(跳变间隔 = 符号率级,已无频偏信息)。两者的 `pulse_width_us` 直方图、`symbol_rate_bps` 甚至 `edge_count` **量纲完全不同**,若混为一谈,AI 会把"过零点间隔"当成"符号宽度"从而误判协议。bit1 缺省(`0`)表示"未声明的模拟前端序列",是保守取值。
>
> 另见 §3.4:`mod:"2fsk"` 有一条**软件补不了的硬件前提**——RMT 只采数字边沿,2FSK 必须先经比较器/过零整形才能进入捕获路径,详见固件架构文档 §4。

> 典型体积:1000 跳变 ≈ 3KB。1000 跳变足以覆盖绝大多数 433MHz 遥控器一帧。

### 3.6 特征提取(设备侧,供 AI 消费)

设备把原始捕获**归纳成 `features`**,这是发给 DeepSeek 的唯一信号载荷(避免上传原始码流):

```json
{
  "f_hz": 433920000,
  "mod": "ook",
  "raw_semantics": "undecided",
  "rssi_dbm": -62.5,
  "edge_count": 128,
  "duration_ms": 41.2,
  "symbol_rate_bps": 2400,
  "symbol_rate_confidence": 0.81,
  "pulse_width_us": { "min": 195, "max": 820, "hist": [0.72, 0.27, 0.01] },
  "encoding_guess": "pwm",
  "encoding_confidence": 0.66,
  "repetition_ms": 20.1,
  "payload_bits": 24,
  "payload_bits_confidence": 0.4,
  "preamble_bits": 12,
  "crc_guess": "crc8_ poly?0x07",
  "crc_confidence": 0.22,
  "frame_repeats": 5,
  "noise_floor_dbm": -95.0,
  "incomplete": false
}
```

**`features` 的三级不确定性表达(强制)**

设备侧特征提取里有三类字段,语义强度完全不同,**必须靠字段自身表达出来**,否则 App 与 DeepSeek 无法区分"确定"与"瞎猜":

| 类别 | 字段 | 表达方式 |
|---|---|---|
| **测量量**(可直接测) | `f_hz` `rssi_dbm` `edge_count` `duration_ms` `pulse_width_us` `noise_floor_dbm` `frame_repeats` | 直接给值,无需置信度 |
| **估计量**(算法给出,可能不准) | `symbol_rate_bps` `repetition_ms` | 必带同名 `*_confidence`(0.0–1.0) |
| **猜测量**(本质是猜测) | `encoding_guess` `payload_bits` `preamble_bits` `crc_guess` | 必带同名 `*_confidence`;**证据不足时必须把值置 `null` 而不是给一个低置信度的胡猜** |

规则:

1. `*_confidence` 取值 `0.0`–`1.0`(`0` = 纯猜测,`1` = 确定),**或 `null`**。`null` 与"字段缺失"的语义**不同**,见规则 5
2. 任何 `*_guess` 类字段**允许为 `null`**;`null` 的语义是"**设备判定无足够证据,拒绝猜测**",与"猜错了的值"是两回事
3. `incomplete`:`true` 表示本次特征是基于**被截断**(`CAPTURE_DONE.truncated=true`)或**丢帧**的数据算出来的 → **AI 必须降低结论强度,App 必须显式标注"数据不完整"**
4. `raw_semantics` 镜像 §3.5 的 `FLAGS` bit1,取值三选一:
   - `"undecided"` — 未经判决的模拟前端数字化序列(比较器/过零整形输出,**保留时序**)
   - `"decided"` — 芯片内部已判决的位流(时钟已恢复,**时序信息已丢失**)
   - `"unknown"` — **设备无法判断**(例如固件未接判决点信息,或该驱动不提供)

   **这个字段决定 `symbol_rate_bps` 与 `pulse_width_us` 该怎么解读**——见 §3.5。设备**不得**在无法判断时默认填 `"undecided"`:那等于谎报载荷语义。
5. **字段缺失时的降级规则(强制,两端同源)**:v1.1 新增字段(`raw_semantics` / `*_confidence` / `incomplete`)**可能因固件较旧而不出现**。缺失时的处理**不是各端自行猜测**:

   | 情况 | 含义 | 消费方(App/AI)必须怎么做 |
   |---|---|---|
   | `*_confidence: 0.4` | 设备给了估计且自知不确定 | 按该值权衡;`< 0.5` 时结论必须标为弱推断 |
   | `*_confidence: null` | 设备**明确拒绝**给置信度 | 与"缺失"同等待遇,但可断言设备支持 v1.1 |
   | 字段**缺失** | 设备**可能不支持 v1.1** | 一律按"**未知/低**"处理,**绝不默认当真**;不得推断为 1.0 |

   **核心原则:缺失 = 未知,不等于"确定"。** 任何把缺省当 `1.0` 的实现都是缺陷(见测试用例 TC-034c)。

> **为什么这比"多几个字段"重要**:`crc_guess` 的示例值 `"crc8_ poly?0x07"` 里那个 `?` 就是不确定性的赤裸痕迹——把它塞进一个 `string` 里传给大模型,模型会当成**事实**读。给 `crc_confidence: 0.22` 之后,模型才有可能说"疑似 CRC8,但置信度低,建议进一步验证"。**这是防止 AI 幻觉的第一道闸门,且必须在设备侧完成,因为设备是唯一知道证据强弱的角色。**

### 3.6.1 时间基准(强制单一口径)

| 字段 | 基准 |
|---|---|
| `t_ms`（`SCAN_HIT` / `RSSI_SAMPLE`） | 设备**单调 `uptime_ms`**(自本次上电起算,单位 ms)。**不是**绝对时间:设备无 RTC,不提供 Unix 时间。跨重启不可比较,App 若需绝对时间须自行记录"握手时刻 + uptime 偏移" |
| `sample_clock_hz` | **采样时钟频率**,不是时间戳。与 `dt_us` 相乘可把 RLE 还原成采样点序号,与挂钟时间无关 |

> **为什么必须写死基准**:若一端当成绝对时间、另一端当 uptime,所有"重复周期/占空比"的跨会话比较都会静默错误。设备**不提供**绝对时间,所以任何需要绝对时间的结论(App 的报告、审计导出)都必须由 App 侧补。

### 3.7 发射与动作

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x40` | `TX_REQUEST` | App→设备 | `{f_hz, mod:"ook"\|"2fsk", payload_rle, repeats, power_dbm, auth_token, reason}`;两段式发射的**第二次确认**复用本消息并**只带** `{armed_ack:true}`(见下)。**`mod` 不含 `"auto"`**:`auto` 是接收侧的自动选择语义(§3.4),发射必须由 App 指定具体调制 |
| `0xC0` | `TX_STATUS` | 设备→App | `{state:"denied"\|"armed"\|"sent"\|"failed", code, detail, granted_power_dbm}` — `granted_power_dbm` 见下 |
| `0x41` | `TX_ABORT` | App→设备 | `{}` |
| `0x42` | `SAVE_SIGNAL` | App→设备 | `{name, features, raw_ref}` — 存入设备本地 |
| `0x43` | `REPLAY_SIGNAL` | App→设备 | `{name, repeats, auth_token}` |

**`payload_rle` 的编码(App↔设备两个方向必须一致)**

§3.5 的 RLE 布局此前只定义了**设备→App**方向,导致 `TX_REQUEST` 无据可依。现明确:**两个方向复用同一布局**,即

```
+--------+------------------+---------------------------+
| FLAGS  | edge[0] 起点     | RLE 对 (dt_us, level) ... |
| 1B     | u32 LE 相对微秒  | 每对: u16 LE dt | u8 level |
+--------+------------------+---------------------------+
```

- 在 **JSON 消息**里,该结构以 **base64url 字符串**承载(而非二进制帧),因为控制帧的 `PAYLOAD` 必须是 UTF-8 JSON(§2.1)。目前**只有 `TX_REQUEST.payload_rle` 承载原始码流**
- `REPLAY_SIGNAL` **不承载**该结构:它按 `name` 引用设备本地已存的信号(捕获时已存过),故其载荷只有 `{name, repeats, auth_token}`。**回放的码流不经过 App**,这也是"回放不绕过门禁"之外的另一层好处——省带宽且避免 App 侧篡改码流
- 在 **`DATA_CHUNK`**(0xB0)里,同一结构直接作为二进制块承载
- **统一布局的理由**:App 常做"捕获→回放"闭环,若两个方向布局不同,就必须在 App 侧做一次无谓且易错的转码
- `FLAGS` 的 bit1(判决点)在**发射**方向无意义(发射的是电平序列),发送方置 `0`

**`TX_STATUS.state = "armed"` 的语义(此前未定义)**

| 状态 | 含义 | 后续 |
|---|---|---|
| `denied` | 门禁未通过,未发射 | 终态;按 `code` 提示,**不得重试** |
| `armed` | 门禁**全部通过**,设备已进入发射就绪态但**尚未开始辐射** | 设备在 `CAPS.limits.arm_timeout_ms`(建议 5s)内若未收到 App 的**再次确认**(第二次 `TX_REQUEST`,**只带** `{armed_ack:true}`),则**自动回落**为 idle 并回 `TX_STATUS{state:"failed", code:"arm_timeout"}`。**该字段只属第二阶段**,第一阶段仍须带完整参数与令牌 |
| `sent` | 已发射完成(含 `repeats` 全部重复) | 终态 |
| `failed` | 已授权但发射过程失败(超时/硬件错误) | 终态;写审计 |

> **`armed` 是"两段式发射"的中间态**,作用是给用户一次"最后一次反悔"的机会。它**不是**"正在发射"——在这段时间内**射频必须零输出**(测试用例须断言 `tx()` 调用次数 = 0)。`arm_timeout` 属 §6 新增错误码。

**`TX_STATUS.granted_power_dbm`(2026-09-21 新增,关闭一处真实缺口)**

原契约只写了"审计须记实际生效功率",但 `TX_STATUS` 的字段集合里**没有承载它的字段**,而 schema 是 `additionalProperties:false` ⇒ **固件按契约实现反而无法回报该值**。现补上:

- **类型**:`powerDbm`(number,可带一位小数)。**可选字段**,因为 `denied` 时并不存在"生效值"。
- **语义**:**本次发射实际生效的功率**,即 `min(TX_REQUEST.power_dbm, policy.tx_power_cap_dbm, 该模块 tx_max_dbm)`。**发起夹取的判定与计算在设备侧**(App 无权重算)。
- **出现时机**:`state ∈ {armed, sent, failed}` 时**必须**给出;`denied` 时可省略。
- **与请求值的关系**:`granted_power_dbm` **优先于** App 本地记录的请求值。App **必须**用它在确认弹窗/结果卡片/审计镜像里显示,而**不是**显示自己发出去的数字。
- **为什么必须有**:`M2(AS07)` 的 `tx_max_dbm` 是 10。若 App 请求 20 而界面显示 20,用户会以为"已在 20dBm 发射",**而实际只发了 10dBm** —— 这是"静默谎报"的一种,与 §4.3 和判据 A-06 要禁止的失效模式同源。有了该字段,UI 才能显示"**实际 10dBm(受模块上限限制,非策略限制)**"。
- **`denied` 时不带该字段是有意的**:`denied` 意味着没有发射,给出一个"生效功率"会暗示"发过了"。

### 3.8 存储与系统

| TYPE | 名称 | 方向 | 载荷要点 |
|---|---|---|---|
| `0x50` | `LIST_SAVED` / `0xD0 SAVED_LIST` | 双向 | 已存信号清单 |
| `0x51` | `DELETE_SAVED` | App→设备 | `{name}` |
| `0x52` | `GET_STATUS` / `0xD2 STATUS` | 双向 | 温度、供电、Wi-Fi RSSI、模块状态 |
| `0x53` | `SET_NET` | App→设备 | `{ssid, pass}` — 现场配网 |
| `0x54` | `OTA_BEGIN` | App→设备 | 预留,见 §9 非目标 |

## 4. 能力集 `CAPS`

App **必须**以 `CAPS` 为唯一真实来源,不得硬编码频段。

```json
{
  "proto": 1,
  "fw": "0.1.0",
  "device_id": "RFLAB-A1B2",
  "active_slot": 1,
  "transports": ["usb", "ble", "wifi"],
  "active_transport": "usb",
  "modules": [
    { "slot": 1, "chip": "si4463", "present": true,
      "bands": [[425000000,525000000]],
      "tx_max_dbm": 20, "tx_allowed": false },
    { "slot": 2, "chip": "cc1101", "present": true,
      "bands": [[387000000,464000000]],
      "tx_max_dbm": 10, "tx_allowed": false },
    { "slot": 3, "chip": "nrf24l01p", "present": true,
      "bands": [[2400000000,2525000000]],
      "tx_max_dbm": 20, "tx_allowed": false }
  ],
  "policy": { "tx_whitelist": [], "tx_power_cap_dbm": 20, "tx_requires_token": true },
  "limits": { "ble_frag_total_max": 15, "ble_frame_max": 3600, "usb_frame_max": 16384, "max_edges": 16384, "stream_rate_hz": 50, "arm_timeout_ms": 5000 }
}
```

> ⚠️ **`modules[].bands` 必须是模块级、不是芯片级**(本示例已于 2026-09-20 按官方手册更正)。此前示例误用了芯片能力(SI4463 写成 142–1050MHz、CC1101 写成三段),**那会让 App 以为能扫到实际扫不到的频段**——正是 ADR-0001 §5.3 与判据 A-06 要禁止的失效模式。真实值:**M1 425–525MHz、M2 387–464MHz、M3 2400–2525MHz**。固件**必须**按实际装的前端上报,不得按芯片能力上报。

`policy.tx_whitelist` 为空 = **禁止一切发射**。出厂默认即为空。

> **⚠️ `policy.tx_power_cap_dbm` 的出厂口径 = 20(2026-09-21,C-11 + NQ-03 关闭)**:产品在屏蔽室内使用,**所有模块按各自模块上限满功率出厂**,故上例取 `20`(原示例为 `10`)。含义变化需要在实现时注意:
> - 这个字段是**设备级全局最大值**,**不是"每模块的值"、也不是"按频段的表"**。某次发射的**有效上限 = min(请求值, 本字段, 该模块 `tx_max_dbm`)**。按当前硬件,`min(20, module)` 得到:M1 = 20、**M2 = 10**、M3 = 20 —— **M2 达不到 20 是模块硬件所致,不是被策略限住**,这条区别必须体现在 UI 文案上(见 App 文档 §4.3);
> - 功率门从此**不再承担法规限幅职责**(C-11/NQ-03:法规限幅已非设计输入),只防"超过模块/硬件能力"。真正的约束落在**频段白名单**与**显式用户确认**上;
> - **`power_dbm` 必须可由用户在 App 内调节**(C-12)。出厂默认是满功率,但契约不承认"功率不可调"这一实现——`TX_REQUEST.power_dbm` 本就是逐次参数,设备**不得**忽略它而恒用上限;
> - **设备回报的实际生效值优先于请求值**:`TX_STATUS` / 审计中的 `granted_power_dbm` 与 `power_dbm` 同时存在时,**以 `granted_power_dbm` 为准**(见合规文档 §4.5)。

**`transports` 与 `active_transport`**:设备支持的链路清单与当前承载会话的那一条(§2.2 活动链路唯一性)。`active_transport` 会在链路切换时变化,App 应以 `HELLO_ACK` / `STATUS` 的最新值为准。

`limits` 各项:

| 字段 | 含义 |
|---|---|
| `ble_frag_total_max` | BLE 分片上界,固定 `15`(4bit `FRAG_TOTAL`) |
| `ble_frame_max` | **当前 BLE 连接下单个完整帧的最大 `LEN`**(按实际协商的 ATT 载荷算出,见 §2.2.1)。**App 必须读它来定请求规模**;Wi-Fi 连接下此值不代表 Wi-Fi 上限 |
| `usb_frame_max` | **USB 链路上单个完整帧的最大 `LEN`**(见 §2.2.2)。USB 无 BLE 的分片约束,故可比 BLE 大得多;但**仍须**用它约束 `DATA_CHUNK` 的分块大小 |
| `max_edges` | 单次捕获跳变数硬上限;请求值超出即**钳制**并回 `WARN{edge_clamped}`(§3.4) |
| `stream_rate_hz` | 高频消息令牌桶速率(§2.4) |
| `arm_timeout_ms` | `TX_STATUS{state:"armed"}` 的等待窗口(§3.7)。超时未收到二次确认则回落 `failed`/`arm_timeout`。**必须有承载,否则 App 无法知道设备的实际窗口** |

## 5. 状态机

```
        ┌──────────┐  HELLO/HELLO_ACK   ┌──────────┐
        │   IDLE   │ ─────────────────► │  READY   │
        └──────────┘                    └────┬─────┘
             ▲                               │ SELECT_MODULE
   ERROR/断开 │                              ▼
             │                         ┌──────────┐
             └──────────────────────── │  MODULE  │
                                       │  READY   │
                                       └────┬─────┘
                          START_SCAN / CAPTURE_START
                                            ▼
                                    ┌───────────────┐
                                    │ SCAN/CAPTURING│
                                    └───────┬───────┘
                                            │ DONE / ABORT / TIMEOUT
                                            ▼
                                        SCAN_DONE
```

规则:

1. `IDLE` 下除 `HELLO` 外一切命令回 `ERROR{not_ready}`
2. `SCAN` 与 `CAPTURE` 互斥;`CAPTURE_START` 在扫描中会先隐式停扫
3. 发起新任务会**隐式中止**进行中的任务(不是排队),并发 `WARN{preempted}`
4. 任何状态收到 `ABORT` 均回 `IDLE/READY`,并保证射频进入 idle

## 6. 错误码

| code | 含义 | App 应对 |
|---|---|---|
| `not_ready` | 未完成 HELLO | 重握手 |
| `version_mismatch` | 协议版本不符 | 提示升级 |
| `module_absent` | 目标模块未检测到 | 重新 `LIST_MODULES` |
| `module_busy` | 模块被占用 | 重试或先中止 |
| `band_out_of_range` | 频率超出模块能力 | 用 `CAPS.bands` 夹取 |
| `band_clipped`(WARN) | 部分频段被裁剪 | 提示真实覆盖 |
| `tx_denied` | 违反发射策略 | 展示原因,不得重试 |
| `tx_bad_token` | 授权令牌无效/过期 | 重新授权 |
| `frag_timeout` | 分片重组超时 | 重发请求 |
| `frame_too_large` | 帧超出当前承载上限(如 BLE 上 `LEN > ble_frame_max`) | 降低请求规模后重发;不得重试同尺寸 |
| `arm_timeout` | 已 `armed` 但未在窗口内收到二次确认,自动回落 | 重新走一次两段式发射(§3.7) |
| `throttled`(WARN) | 限流丢数据 | 降采样或缩短捕获 |
| `preempted`(WARN) | 进行中的任务被新任务**隐式中止**(§5 规则 3) | 无需处理;若 App 正处于该任务的 UI 流程中,应把结果标为"被抢占"而非"失败" |
| `edge_clamped`(WARN) | `max_edges` 请求值超上限被钳制 | 读 `CAPS.limits.max_edges` 后重发 |
| `hw_error` | SPI/RMT 异常 | 报错并建议重启模块 |
| `unsupported` | 未知消息类型 | 忽略 |

## 7. 安全

| 面 | 要求 |
|---|---|
| Wi-Fi 通道 | 设备侧 WebSocket 支持 `wss://`(自签证书 + 指纹校验,或局域网信任模式);默认仅监听局域网 |
| 配网 | 首次配网**必须经 USB**(本批唯一可信配对信道,见 §7.1(1));**不经明文广播密码**。Wi-Fi 凭据写入沿用带内 `SET_NET`;**"设备当前 IP/SSID 可经 USB 读出"是 NQ-01 方案 A 的承诺,但对应字段尚未定义(见 §9.1),实现前不得宣称已兑现** |
| 设备身份 | `HELLO_ACK.device_id` + 首次配对绑定的长期密钥,防串台 |
| 发射授权 | `tx_requires_token` 为真时,`TX_REQUEST` 必须带 App 计算的授权令牌(HMAC-SHA256,一次性)。**注意:令牌没有"有效期"概念**——防重放靠单调 `seq` 而非时间,因为设备无 RTC。完整定义见 §7.1 |
| 审计 | 每次 `TX_REQUEST`(含被拒的)写本地环形日志,含时间、频率、功率、结果 |
| 密钥 | **DeepSeek API Key 绝不进设备**(见 ADR-0002) |

### 7.1 发射授权令牌 `auth_token` 的可实现定义

原文只写了"App 计算的短时令牌(HMAC,30s 有效)"而没有说**密钥从哪来**,也没有说清**防重放的依据**——这使得固件无法实现校验。本节补齐,目标是"**离线可用 + 防重放 + 抗串台**",不引入云端依赖。

**(1) 长期密钥的建立(首次配对)**

```
设备开机若未配对:生成 32B 随机 pairing_secret(用 ESP32-S3 硬件 RNG)
  → 存 NVS(不可读回;只可被校验逻辑调用)
  → 同时生成 6 位一次性配对码 pair_code(本批无显示载体,见下)
App   → HELLO{client_id, pairing:true}            # USB 链路:不要求 pair_code
设备  → HELLO_ACK{pair_ok:true, first_bond:true, pairing_secret:"<32B base64url>"}   # 仅 USB(BLE 本批回 no_display)
   或  HELLO_ACK{pair_ok:false, pair_error:"<见下表>"}
```

- `pair_code` 的作用是**防中间人**:配对请求若被中间人截获,攻击者拿不到设备端的一次性码
- `pairing_secret` 只经**可信链路**下发一次,即 **USB**(ADR-0003:要求操作者物理在场);**Wi-Fi 路径不下发**,只用于之后的令牌校验。
  > ✅ **2026-09-21 定稿(用户选方案 A)**:USB 正式列为可信配对信道,**且是本批唯一的配对路径**。此前"BLE 加密链路或 USB"的表述收窄为**仅 USB** —— 原因见下条(`pair_code` 无搬运通道时不得免码配对,BLE 因此在本批不承担配对职责)。原 Q-09 关闭。
- **`pair_code` 的来源**:设备端生成 6 位。⚠️ 原定由 **OLED 显示**(C-07),但 **OLED 已延期到下一批开发**,因此**本批没有显示载体**(契约 §7.1(1) 定稿):
  - **USB 链路上 `pair_code` 不作为必要条件** —— USB 是**物理端口**,连接行为本身已证明"操作者在场",它不经过任何射频,故原设计要防的"BLE 被中间人截获"在 USB 上不成立 ⇒ **USB 配对成功不产生 `bad_code`**(它根本不看这个字段)。
  - **BLE 链路上 `pair_code` 仍是必要条件**,而本批**没有码的搬运通道** ⇒ **BLE 配对请求一律回 `pair_error:"no_display"`**。这不是"降级",而是**唯一正确的结果**:不得为了"无屏也能用"把它改成可选 —— **静默降级会让防中间人机制名存实亡,而调用方看不出来**。
  - ⚠️ **本批 `bad_code` 是保留值,无活路径**:USB 不校验码,BLE 在拿到码之前就已回 `no_display`。实现时**不要**为它造路径,测试也**不要**为它造用例(见测试计划 TC-104b 注)。它保留是为了下一批 OLED 到位后启用。
  - 替代搬运通道的候选(机身标签静态码 / LED 计数序列)与取舍见产品定义 §8.2 —— **两者均未采纳**。
- 一台设备只与一个 `client_id` 配对(主绑定);重新配对其他 `client_id` 需**设备端物理按键确认**(防远程劫持)。⚠️ **USB 的在场性替代不了这条** —— USB 防的是"远距离截获配对",防不了"别人趁你不在插线改绑"
- **`pairing_secret` 的承载** = `HELLO_ACK` 的扩展字段(仅在**可信链路 USB** 上出现)。不新增消息类型,理由:配对是握手的一部分,拆成独立消息会引入"半配对"中间态
- ⚠️ **只显示/只搬运配对码,绝不搬运 `pairing_secret` 或任何可用于发射的凭据**。理由:人眼可读的通道(屏幕、标签、灯光)是一条**带外信道**,经它传递的密钥意味着不在设备旁边的人也能取得(拍照/录屏/远程视频),会破坏"发射必须在场"的红线。配对码是一次性的、且必须配合物理在场才能使用,性质不同 —— 见 App 文档 §3.1

**(1.1) 配对失败的表达(`pair_error`)**

只定义 `pair_ok:true` 是不够的——App 必须能区分"码打错了"和"设备已被别人绑定",否则无法给出正确指引:

| `pair_error` | 含义 | App 应如何引导 |
|---|---|---|
| `bad_code` | `pair_code` 不匹配 | ⚠️ **本批为保留值,无活路径**:USB 不校验码,BLE 在取到码之前即回 `no_display`(见 §7.1(1))。下一批 OLED 到位后启用:让用户重看设备端码重新输入,连续失败应限速 |
| `no_display` | **设备当前没有 `pair_code` 的搬运通道**(本批 OLED 延期 ⇒ **BLE 上必然如此**) | **不得**提示"重试";应提示改用 **USB 有线连接完成首次配对**(见 (5) 与产品定义 §8.1),或说明本设备需加装显示模块 |
| `already_bound` | 设备已绑定其他 `client_id` | 提示需在**设备端按键**才能改绑,而不是反复重试 |
| `ble_only` | 在 Wi-Fi 链路上尝试配对 | ⚠️ **本批语义已变**:不再是"改用 BLE",而是**"改用 USB"** —— BLE 在本批同样不能配对(回 `no_display`)。保留该错误码以区分"链路不对"(Wi-Fi)与"链路上没有码的载体"(BLE) |
| `rng_failed` | 设备侧随机数/RNG 初始化失败 | **拒绝配对**并报错;不得降级为弱随机 |

**(1.2) `seq` 基线与失步恢复(防永久锁死)**

设备**应**在 `HELLO_ACK.last_accepted_seq` 中回传自己已接受的最大 `seq`(**无符号整数,未发射过任何信号时为 `0`**)。

- App 每次成功 `HELLO` 后,把自己的计数器置为 `max(本地值, last_accepted_seq)` 再继续递增
- **为什么必须**:若 App 清空数据/重装/换机,本地 `seq` 从 0 重新开始,而设备要求 `seq` **严格大于**已接受最大值 → 会**永久拒绝一切发射**,且错误码只有 `tx_bad_token`(见下),用户**无法自救**。有了该字段,App 可对齐基线自然恢复
- **重新配对会重置该基线**:新的 `pairing_secret` 建立时,设备同时把 `last_accepted_seq` 归零(旧密钥的令牌随之全部失效,这是期望行为)
- **`tx_bad_token` 的恢复路径必须闭环**:App 收到该错误的处置顺序为 ①重新 `HELLO` 取 `last_accepted_seq` 并重置计数器 → ②若仍失败,提示"设备可能已与其它手机配对",引导重新配对。**不得只做无限重试**

**(2) 令牌计算(两端同式)**

```
token = HMAC-SHA256(key = pairing_secret,
                    msg = device_id | seq_be32 | f_hz_be64 | mod | power_dbm_x10_be16)
token_wire = base64url(token[0:16])          # 截断到 128bit,足够
```

| 项 | 说明 |
|---|---|
| `seq` | App 单调递增计数器(每设备独立),设备记录**已接受的最大 seq** |
| `device_id` | 绑定设备身份,防令牌跨设备重放 |
| 频率/功率/调制 | 绑定到**这一次具体发射**,令牌不能挪用于改频或加功率 |

**(3) 校验规则(设备侧,全部不通过即 `ERROR{tx_bad_token}`)**

1. 防重放:`seq` 必须 **严格大于**已接受的最大值;不通过即拒
2. 参数绑定:重算 HMAC,**常数时间比较**(防时序侧信道)
3. 令牌**一次有效**,校验通过后立即记录 `seq`
4. **不依赖任何绝对时钟**:设备只用 `uptime_ms` 做日志与相对时间戳。**设备无法、也不应判断"令牌是多少秒前签发的"**

> **为什么不用"时间戳 + 共享密钥"的朴素写法**:设备没有可信 RTC(ESP32-S3 掉电即失时间),开机后时间不可信,基于时间戳的窗口校验会要么形同虚设、要么误拒。用 `seq` + `uptime` 相对窗口是**在无 RTC 硬件上仍能成立**的方案。
>
> ⚠️ **实现陷阱**:若固件按"令牌 30 秒过期"来实现,在无 RTC 的设备上会表现为**恒拒**(时间从未前进到签发时刻)或**恒过**(时间被复位到 0 后所有旧令牌都"新鲜")。**这两种都是缺陷**;正确实现里根本不存在"过期"这个概念,只有 `seq` 单调性。

**(3.1) `tx_bad_token` 的恢复闭环**

`tx_bad_token` 是**可恢复**错误,但恢复动作不是重试,而是重新对齐基线:

```
收到 tx_bad_token
  → 重新 HELLO,读 HELLO_ACK.last_accepted_seq
  → 本地 seq := max(本地 seq, last_accepted_seq) + 1
  → 重签令牌再试一次(仅一次)
  → 仍失败 → 提示「设备可能已与其它手机配对,请在设备端按键后重新配对」
```

**禁止无限重试**——`seq` 失步是确定性失败,重试永远不会成功(见 (1.2))。

**(5) ⚠️ 一条必须显式承认的设计结论:纯 Wi-Fi(且不接线)的设备无法发射**

`pairing_secret` **只经可信链路(本批为 USB)下发**,因此在**只有 Wi-Fi、且不插线**的链路上:

- 设备**永远拿不到 `pairing_secret`** → 无法完成绑定
- 于是 `tx_requires_token` 永远无法满足 → **该设备永久不可发射**

这不是 bug,是"发射默认拒绝 + 密钥必须经带外可信信道下发"两条原则的直接推论。但它有一个**必须在产品层面拍板的后果**:

> **原结论(已过时,留档)**:"在 iOS 上(iOS 全平台不支持 Web Bluetooth),纯 PWA / 纯 Wi-Fi 路径的应用将无法让设备发射任何信号。" —— **iOS 已于 2026-09-20 放弃**(C-06),故该表述不再适用。
>
> **2026-09-21 更新**:`pairing_secret` 的下发信道为 **USB**(ADR-0003;要求物理在场)。因此"Wi-Fi-only 设备永久不可发射"**不再是一个无法绕开的死结** —— 用户插上 USB 线即可完成绑定。**但"纯 Wi-Fi 且不接线"的设备仍然永久不可发射**,这条结论保留。
>
> ✅ **2026-09-21 定稿(用户选方案 A)**:本批**只以 USB 配对**(见 §7.1(1))。BLE 在本批能收发命令、但**不能完成首配**,故"BLE 不可用"不再等于"无法绑定"。

处置选项(曾登记为 Q-09,**已关闭**):

| 方案 | 说明 | 结论 |
|---|---|---|
| **A. 接受** | Wi-Fi-only 用户只能用扫频/捕获/分析,**发射必须能插 USB 线** | ✅ **采纳**(与 D 合并:本批只走 USB) |
| B. 设备端本地授权 | 在设备上加物理按键/拨码,长按即临时授权发射(不经 App) | ❌ 未采纳:削弱"远程即不可发射",且需硬件改动 |
| C. 带外传递密钥 | 设备屏幕显示助记词/二维码,用户在 Wi-Fi 页面手动录入 | ⏸ **本批不可行**:OLED 已延期,设备无屏;且**密钥经人眼/摄像头传递**本就被 §7.1(1) 红线禁止 |
| **D. USB 作为可信配对信道**(2026-09-21 新增) | USB 可下发 `pairing_secret`;物理端口天然要求在场 | ✅ **采纳,且定为本批唯一配对路径**(对应 Q-09 关闭) |

**本契约按方案 A + D 定稿**;若将来改选 B 或 C,必须回头修订本节并在合规文档中重新评估"远程不可发射"这一红线是否仍成立。

**(4) 与"四道门禁"的关系**:`auth_token` 只是**第四道**(授权)门。前三道——频段白名单、功率上限、显式用户确认——**任何一道不过即拒绝,且不因令牌有效而放宽**。见固件架构文档 §8。

## 8. 版本演进

- `VER` 只在**帧格式**变化时升。新增消息类型 / 新增字段**不升版本**
- 移除字段前必须先在 `CAPS` 中标记 `deprecated:[...]`,保留至少一个小版本
- App 声明 `proto_min/proto_max`,设备取交集;无交集即 `ERROR{version_mismatch}`
- **v1.1 的处理方式**:本次修订新增了消息类型与字段,但**未改动帧格式**,故 `VER` 仍为 `0x01`;不兼容变更集中在 §2.2.1(`LEN` 实际上限收紧)与 §3.3(`SCAN_DONE` 字段改名 `hits` → `hits_total`)。**`SCAN_DONE.hits` → `hits_total` 属破坏性改名**,按上条规则应先标 `deprecated` 保留一个小版本;因本项目**尚未有任何已发布固件/App**,选择直接改名而不留兼容层。若在 v1 固件已流出后再改,则必须走 `deprecated` 流程。

## 9. 非目标

- ❌ 不做 OTA 固件升级(本期 §3.8 `OTA_BEGIN` 仅占位)
- ❌ 不做设备间互联/Mesh
- ❌ 不做云端中转;Wi-Fi 场景是**同网段直连**
- ❌ 不传原始 IQ 采样(带宽不允许),只传跳变序列 + 特征

### 9.1 已知未定义 / 显式推迟到 v1.2 的项

以下**不是遗漏,而是本轮有意延后**——写在这里是为了让固件/App/schema 三方都知道"现在是不知道,不是随便填":

| 项 | 现状 | 为什么推迟 |
|---|---|---|
| `STATUS` 的字段名 | §3.8 只写"温度、供电、Wi-Fi RSSI、模块状态",无字段名 | 需先定硬件采样点与单位(见硬件文档 H-Q3 待实测);现在编字段名会与实测口径打架 |
| `SET_NET` 的结果回报 | 缺 `NET_STATUS{ok,ip,ssid,err}` → App 只能轮询猜配网成败 | 可在 v1.2 与 `STATUS` 一起定义,避免两次改动 |
| 读取设备侧审计 | 缺 `GET_AUDIT`/`AUDIT_LOG` → App 审计镜像(A-05 回执)无法闭环 | 需先定环形日志容量与分页口径 |
| `CAPS.device_name` / `CAPS.bound` | 缺;App 无法显示设备名与绑定状态 | UI 必需但非阻塞;v1.2 与 `regulatory_region` 一并加 |
| `CAPS.regulatory_region` | 缺;App 无法判断设备加载的是哪套白名单常量集 | 依赖 **Q-06(目标使用地)**,未定案前加了也是错的 |
| `SAVED_LIST` / `LIST_SAVED` 的元素结构 | 只写"已存信号清单" | 与 `SAVE_SIGNAL` 是否复用同一结构未定 |
| `PONG.t_ms` 语义 | 未写是回显值还是设备本地时刻 | 影响 RTT 计算精度;v1.2 明确 |
| `wss://` 的信任模型按载体分列 | §7 笼统写"自签证书 + 指纹校验" | **浏览器没有证书指纹固定 API**,该表述对 PWA 路径不可实现,必须区分 PWA(用户装 CA / 内网域名 + 公信证书)与原生(证书固定)。**见下** |
| 设备侧离线记录/回读 | 无此能力 | PWA 前后台必断 → S1 长时无人值守可能不可实现。**要么 v1.2 加"记录会话→回读"消息对,要么在本节显式列为非目标**(待产品决策) |
| 设备当前 IP / SSID 的读取 | §3.8 的 `STATUS` 字段未定义,`SET_NET` 无结果回报 ⇒ **App 拿不到设备 IP** | ✅ **2026-09-21 新增关注**:NQ-01 定案为 USB 配对后,"配网是否成功、设备在哪"变成**必须经 USB 可查**才能兑现"消除 AP 地址靠猜"的承诺。⚠️ **这是契约的已知缺口,不是已实现能力** —— 在 v1.2 补齐前,固件**不得**宣称该承诺已兑现 |
| ~~`TX_STATUS` 缺"实际生效功率"字段~~ | — | ✅ **已于 2026-09-21 关闭**:§3.7 新增 **`granted_power_dbm`**(可选,`armed`/`sent`/`failed` 时必须给出),schema `msg-tx_status` 同步。原缺口是"文档要求审计记生效值,但字段集合里没有它,而 schema `additionalProperties:false` ⇒ 固件照契约实现反而回报不了" |

> **`wss://` 信任模型的诚实说明(重要,已由 App 设计文档独立复核)**:契约 §7 写"自签证书 + 指纹校验",但**浏览器不提供证书指纹固定 API**,因此该方案**只有原生外壳能实现**。PWA 路径现实上只有两条:①让用户手动安装设备 CA;②给设备一个内网域名并签发受信证书。这不是本契约能"补齐"的字段,而是**载体选型的直接约束**(见 Q-05)。
