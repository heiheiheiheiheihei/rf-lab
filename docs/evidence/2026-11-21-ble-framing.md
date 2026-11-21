# 2026-11-21 APP 报 “JSON: … position 666”:BLE 下行 state/codes 漏了 `\n`

状态:根因 `measured`(源码 + 真机 `STATE` 抓取 + 主机复现)· 修复 `flashed`(构建 + 主机测试 + 真机启动/JSON 合法)·
**手机 BLE 端到端已实测:警告消失、命令回显正常、状态栏/码表正常刷新** · 固件 `0.7.1`

**结论:不是 APP 的解析问题,是固件 BLE 下行少了帧结束符。`state`/`codes` 两条消息走裸
`notify_bytes()` 发送(没有结尾 `\n`),而客户端(`BleClient.handleNotify`)按 `\n` 切行、
整行 `JSON.parse` —— 于是 `state` 帧和下一条消息被拼成一行,整行解析失败,两条一起丢。
报错里的 `position 666` 就是那条没带换行的 `state` 帧的字节长度。**

> **影响比“多一条警告”大**:BLE 连接下 `state` 和 `codes` 从来没被 UI 收到过(状态栏冻结、
> 码表为空)。这是代码路径推断(见 §2),真机用 APP 连一次就能验证(§7)。
> 也解释了为什么它一直没被发现:0.6.0 的 BLE 链路此前**从未在真机上验过**
> ([`2026-11-15-ui.md`](2026-11-15-ui.md) §6、[`2026-11-21-android-release.md`](2026-11-21-android-release.md) §5)。

## 1 现象(用户 22:41:15,Android APP)

```
22:41:15> MODE continuous
22:41:15 JSON: Unexpected non-whitespace character after JSON at position 666 (line 1 column 667)
22:41:17> MODE single
22:41:17 JSON: Unexpected non-whitespace character after JSON at position 666 (line 1 column 667)
22:41:22> DISP TEST 30
22:41:22 JSON: Unexpected non-whitespace character after JSON at position 666 (line 1 column 667)
```

- 三条命令各报一次,位置完全相同 → 触发者是**命令引起的一次状态推送**,不是命令回显。
- `Unexpected non-whitespace character after JSON at position N` 的语义:前 N 个字节是**合法
  JSON**,第 N 个字节之后还有内容。即“一条合法消息 + 下一条消息”被拼成了一行。

## 2 根因

`firmware/src/ble_link.c` 的 `tx_task()` 有两条下行路径:

| 消息 | 0.7.1 之前的发送方式 | 结尾 `\n` |
|---|---|---|
| `hello` / `log` / `event` | `notify_line()` → 自己 `snprintf("%s\n")` | ✅ |
| `state` / `codes` | 裸 `notify_bytes(tmp, strlen(tmp))` | ❌ |

客户端:

- `android/.../BleClient.java:393` `handleNotify()`:按 `\n` 重组行,只把完整行交给 JS;
- `web/app.js:244` `safeParse()` / `handleLine()`:整行 `JSON.parse`,失败就
  `log('warn', 'JSON: ' + e.message)` —— 就是用户看到的那行。

所以顺序是:`state`(无 `\n`)→ 下一条消息(带 `\n`)→ 客户端把两者当成一行 →
`JSON.parse` 在 `state` 帧末尾失败 → **`state` 和那条后续消息一起被丢弃**。每次状态变化
(每条命令都会 `app_mark_state_dirty()`)正好产生一次,和日志“一条命令一条警告”完全对上。

`codes` 同理:鉴权后 `cd_seq = 0`,每次都会推一次 `codes`,而它也没有换行 → 码表同样到不了 UI。

## 3 现场取证(真机 `/dev/ttyACM0`,22:44)

`STATE` 命令打出的是 `app_copy_state()` 的裸快照(和 BLE 里发的是同一份),638 字节:

```json
{"fw":"0.6.0","proto":1,"uptime_ms":199836,"net":{"mode":"sta","ap_on":0,"has_creds":1,"ap_ssid":"RF-Lab-1B59","ap_ip":"192.168.4.1","sta_connected":true,"sta_ssid":"Mark","sta_ip":"192.168.5.125","rssi":-38},"ble":{"on":1,"conn":0,"name":"RF-Lab-1B59","pin_set":1},"module":{"baud":9600,"rx_bytes":2,"frames":0,"acks":2,"dups":0,"stray":0,"resync":0},"store":{"count":9,"max":32},"oled":{"present":1,"on":1,"flush":7,"err":0,"last_us":8721,"max_us":8972},"ota":{"armed":0,"pct":0,"phase":"idle","slot":1,"next":-1,"pending":0},"tx":{"active":0,"kind":"idle","id":0,"ok":0,"ms":0},"last":{"kind":"boot","id":0,"band":"","code":"","ms":0}}
```

外加帧头 `{"t":"state","seq":<n>,"data":`(28 字节)和收尾 `}`(1 字节):payload 637 → 帧
**666 字节**,payload 638 → **667 字节**。现场报的是 666,与当时 `uptime_ms`/`rssi` 少一位
数字的同一帧完全吻合(本机探针时刻 667)。

> 上面是现场在跑的 **0.6.0**:`0.7.1` 的 `state` 多一个 `"ver"` 字段,帧会再长 8 字节,
> 故障形态不变(位置 = 帧长),所以不要拿 666 当版本特征。

## 4 主机复现(设备快照 + 客户端的切行/解析规则)

```js
// node 侧照搬 BleClient.handleNotify 的规则:split('\n') 后整行 JSON.parse
const frame = '{"t":"state","seq":9,"data":' + state + '}';   // 无 '\n'
const stream = frame + '{"t":"log","seq":41,"line":"OK MODE continuous"}\n';
for (const l of stream.split('\n').filter((x) => x.trim())) {
  try { JSON.parse(l); } catch (e) { console.log('JSON:', e.message); }
}
```

实测输出:

```
A) live state payload = 638 | frame = 667 | merged lines = 1
   JSON: Unexpected non-whitespace character after JSON at position 667 (line 1 column 668)
B) payload | frame | error position | match
   100 | 129 | 129 | yes
   600 | 629 | 629 | yes
   637 | 666 | 666 | yes        ← 现场那条
   638 | 667 | 667 | yes
   900 | 929 | 929 | yes
C) fixed framing: parsed lines = 2
```

B 行证明不变式:**报错位置 == 未终止帧的字节长度**;C 行证明每条消息自带 `\n` 后逐行可解析。

## 5 修复

只改设备端下行,不动 APP(APP 的按行解析是对的)。文件:`firmware/src/ble_link.c`、
`firmware/src/link.{c,h}`、`firmware/test/host/test_link.c`。

1. **单一发送口**:裸 `notify_bytes()` 换成 `notify_message(json, len)`,帧结束符 `\n` 由它
   统一追加(数据 + `\n` 在同一分片序列里发出,不多一次 notify)。代码里不再存在“能发不带
   换行字节”的入口,同类回归不再靠记性。
2. **分帧抽成纯函数**:`link_frame_fill()`(`firmware/src/link.c`)负责切片与追加 `\n`,
   主机可单测。回归用例覆盖 len ∈ {0,1,2,19,20,21,199,200,201,666,3000} × chunk ∈ {1,20,200},
   断言“拼起来恰好是 `json + '\n'`、每片 ≤ chunk、`\n` 只在末尾”。
3. `state`/`codes` 的 `snprintf` 返回值参与判断:被截断就整帧丢弃,不再发半截 JSON(旧代码
   会发出去,APP 同样解析失败)。

**Key Judgment**:把终止符放进“唯一的发送函数”,而不是在 `state`/`codes` 两处各补一个
`'\n'` —— 后者正是这次出错的形态(两条新路径各自实现,漏了一处);前者让“漏结尾”在类型上
不可表达。

**Avoided Misfix**:不在 APP 里对解析失败的行做“按 `}{` 拆开重试”。那会把一条协议错误
变成客户端猜测,拆出来的两条消息语义也不可信(可能半截);而且旧固件升级后仍需正确路径。
APP 侧保持“整行解析失败 → 丢弃 + 警告”,让固件违约时**响亮地**失败。

## 6 验证

| 项 | 结果 |
|---|---|
| Build | `pio run -e esp32c3` ✅ `[SUCCESS]`,image **1,098,032 B / slot 2,031,616 B(54.0%)**,编译警告 0 条(`grep -c "warning:"` = 0)。**注意:这次构建包含工作树里并行进行中的 codebook 改动**(`firmware/src/codebook.*`、`0.7.1` 版本号),不是本次修复的独立基线 |
| Host tests | `./tools/host_test.sh` ✅ 全绿:rf34dl 76、codebook 218、link **9418**(新增分帧用例)、ota 41、wiring/web/partitions、TUI 12、OLED 1286 |
| 真机烧录 | ✅ 23:02 `tools/flash.sh` 写入 app→ota_0,NVS 保留;设备启动、`STATE`/`/api/*` JSON 合法(§8) |
| 手机 BLE(23:11) | ✅ 两条连接、一次 `DISP TEST 30`:**0 条 `JSON:` 警告**,命令回显 `OK disp=test 30s …` 正常解析(§8) |
| 手机 UI(23:12) | ✅ uptime 每秒走动、码表 9 条 —— `state`/`codes` 帧确实被解析并消费(§8) |
| 可选未做 | nRF Connect 抓包核对每帧末字节 `0A`(行为已由上面两条实测覆盖,非必需) |

## 7 手机侧最后一跳(待做,约 1 分钟)

1. ~~给设备上新固件~~ ✅ 已烧(见 §8)。后续固件更新走 `tools/ota.py` 或 Web UI。
2. ~~手机 APP 连接 → 输 PIN 鉴权~~ ✅ 23:11:05 / 23:11:07。
3. ~~按 `MODE CONT` / `MODE SINGLE` / `OLED TEST`:日志里应没有 `JSON:` 警告~~ ✅ 23:11:25
   `DISP TEST 30`:0 条警告,回显正常(§8)。
4. ~~看状态栏与码表~~ ✅ 23:12:uptime 每秒走动、码表 9 条。
5. 可选(未做):nRF Connect 订阅 TX,`state`/`codes` 每帧最后一个字节应为 `0A`。

## 8 上机结果(2026-11-21 23:02,`/dev/ttyACM0`)

先备份 NVS(`esptool read_flash 0x9000 0x6000` → `/tmp/rflab-nvs-backup-2301.bin`),
再 `./tools/flash.sh /dev/ttyACM0`(bootloader + 双 OTA 表 + app→ota_0,擦 `otadata`,
**不动 NVS**)。

```
# RF-Lab fw=0.7.1 (ESP32-C3 + YS-RF34DL)
# build=97a8351+dirty ui=839508d3
# ota slot=0 next=-1 state=idle pct=0 pending=0
# ble on=on adv=yes conn=no name=RF-Lab-1B59 pin=set
# codebook ver=2 count=9/32 migrated=no
OK stored=9/32 ...
```

| 检查 | 结果 |
|---|---|
| 启动 / 版本 | ✅ `fw=0.7.1`,`ota_slot=0`,无 panic |
| NVS(码库/Wi-Fi/PIN) | ✅ 9 条码原样,Wi-Fi STA 连上(`Mark`,-38 dBm),PIN 仍在 |
| `STATE` JSON | ✅ 整行可解析,含 `"ver":2`;HTTP `/api/state` 645 B 也是合法 JSON |
| `/api/codes` | ✅ 9 条合法 JSON(含中文 name/group 与 emoji `sym`) |
| BLE 广播 | ✅ `adv=yes` |
| BLE TX 逐帧 `\n` | ⏳ **本机无蓝牙适配器,仍未验证** —— 需手机 APP 连一次(看 §7 第 3、4 步) |

> 这次烧的是**工作树镜像**(含并行进行中的 codebook/UI 改动),而提交只含本 BLE 修复;
> 工作树其余改动由对应改动自己提交。设备端行为与提交不是一一对应,验证结论按此理解。

### 手机侧(2026-11-21 23:11,Android APP,连的是 0.7.1 设备)

```
23:07:53  RF-LAB FIELD TERMINAL // BLE // UI 62111123
23:08:28  BLE 已连接 RF-Lab-1B59
23:08:34  BLE AUTH OK
23:08:39  BLE 已断开
23:11:05  BLE 已连接 RF-Lab-1B59
23:11:07  BLE AUTH OK
23:11:25  > DISP TEST 30
23:11:25  OK disp=test 30s (DISP ON to leave test)
```

- 修复前同一操作:`> MODE …` 之后必跟一条 `JSON: … position 666`,而且**命令回显被吞掉**
  (state 帧没换行 → 与 log 帧拼成一行 → 整行丢弃)。§1 的日志里确实一条 `OK …` 都没有。
- 修复后:同一条链路 **0 条 `JSON:` 警告**,并且 `OK disp=test 30s …` 这条 log 帧
  (`execute_one_queued()` → `link_publishf({"t":"log"…})` → BLE)被正常逐行解析。
  这同时证明 `state` 帧不再吞掉后续消息。
- `state`/`codes` 的 UI 效果:23:12 确认 **uptime 每秒走动、码表 9 条**,即两条帧都被逐行
  解析并消费(修复前 BLE 下这两项必然冻住/空)。
- 23:08:39 那次 5 秒后断开 = 手机锁屏/走开(用户确认),不是设备侧断链;23:11 连接持续正常。

## 9 残余问题(未修,记录在案)

- **旧固件(0.7.1 之前,含现场在跑的 0.6.0)仍然会丢 `state`/`codes`**:APP 不做跨行恢复,必须升级固件。APP 本身
  不需要发版(资产未变)。
- `BleClient.handleNotify()` 逐包 `new String(value, UTF_8)`:非 ASCII 的 SSID(`sta_ssid`)
  正好跨通知包时会出现替换字符,进而破坏该行 JSON。当前现场 SSID 是 ASCII(`Mark`),未触发。
- `uconsole`/TUI 若也用行 JSON 订阅,同样受益于这次修复;未单独验证。
