# RF-Lab 协议 JSON Schema(机器可读镜像)

状态:`draft` · 契约版本:`v1.1` · 镜像的协议文档修订:`sha256:61be2900a7f95f4a60fb7575358c23e94731e9fecfa8b89e2320e1f069862999`

> **本次镜像变更记录(2026-09-21,`61be2900…`)**:**schema 文件仍然一个字节都没改。** 契约文档的改动是**本批适用范围声明** —— 用户决定"**本批只做 USB**",BLE 与 Wi-Fi **移出本批**([ADR-0005](../../docs/adr/0005-usb-only-batch-and-deferred-wireless.md))。契约里改的是 **§2.2 的承载表加 ⏸ 标注、§7.1(1) 的 BLE 分支与 §7.1(1.1) 的 `ble_only` 标注"本批无活路径"、以及 §7.1(5) 的措辞**。
>
> ⚠️ **本次刻意不删任何字段** —— 包括 `CAPS.limits.ble_frame_max` / `ble_frag_total_max`。理由:契约是**传输无关**的,`V-12` 的 BLE 帧上限推导是**一次已完成且修正过两处旧口径错误**的推导;删字段既会让下次纳入无线时重推(很可能重蹈原错),**又是对既有实现的不兼容变更,而本批没有任何功能需要它**。⇒ **机器可读镜像保持不变,仅重 pin 文档 sha256。**
>
> 🔴 **不要因为镜像 hash 变了就去找 schema 的 diff** —— 那里**没有 diff**(与上一版 `df9b4929…` 的情形相同)。

> **上一版镜像变更记录(2026-09-21,`df9b4929…`)**:schema 文件一个字节都没改。契约文档的改动是**非目标声明** —— 用户取消「无人值守远程部署」后,**"设备侧离线记录 / 回读"由"待决"转为"显式非目标"**(契约 §9),并删掉 §9.1 里那一行重复登记。由于该能力**从未定义过任何消息**,本次契约修订**不涉及任何字段、消息或 `$defs`**。

> **镜像修订变更记录**:上一版镜像 `d33d5e8b…`(2026-09-21 C-11 + OLED 延期)。本次镜像 `9659d6be…`,对应契约 2026-09-21 的追加修订(**NQ-01/NQ-03 定稿**):**① §7.1(1) 首次配对定稿为「只走 USB」** —— `pair_code` 在 USB 上不校验,BLE 配对一律回 `pair_error:"no_display"`;`pairing_secret` 的下发信道由「BLE 或 USB」收窄为**仅 USB**;**② §4 功率口径改为「所有模块各自满功率出厂 + 可由 App 调节」**(C-11 修订 + C-12),`policy.tx_power_cap_dbm` 明确为**设备级全局最大值**,有效上限 = `min(请求, cap, 模块 tx_max_dbm)`;**③ §3.7 新增可选字段 `TX_STATUS.granted_power_dbm`**(唯一一处**字段集合变化**;原契约要求审计记"实际生效功率"却无承载字段,而 schema 为 `additionalProperties:false` ⇒ 固件照契约实现反而回报不了)。帧格式未变、`VER` 不变,仍为 36 消息 + 30 `$defs`;新增字段为**可选**,对既有实现向后兼容。

> **权威关系**:[`docs/30-protocol/010-device-app-protocol.md`](../../docs/30-protocol/010-device-app-protocol.md) 是唯一权威来源。
> 本目录只是它的**机器可读镜像**,用于开发期校验。**schema 与文档冲突时,以文档为准,并应当立刻修正 schema。**
>
> ⚠️ 该文档正在被**并行修订**(本次镜像期间从 279 行涨到 563 行,契约版本 v1 → v1.1)。
> 本目录镜像的是上面那个 sha256 对应的修订。用 `sha256sum docs/30-protocol/010-device-app-protocol.md` 比对;
> 不一致即说明文档已变更,请按 §5 的清单重新同步。

---

## 1. 用途

- 给固件(App 侧解析、设备侧序列化)与 App 提供可编程的载荷契约,在**开发期/测试期**抓出字段拼写错误、类型错误、枚举越界;
- 给 AI 提示词(`docs/50-ai/`)与测试用例(`docs/70-test/`)提供字段清单的单一事实来源;
- 在协议文档评审时,用 `git diff protocol/schemas/` 直观看到"这次改动到底动了哪些字段"。

**作用范围**:本目录的 schema 只描述**帧头之后的 PAYLOAD**(协议文档 §2.1、§3)。
帧头(`MAGIC/VER/TYPE/SEQ/LEN`)、BLE 分片头、以及 `ble_frame_max` 背后的分片预算推导(§2.2.1、§2.3)属于传输层二进制布局,**不在本目录覆盖范围内**。

## 2. 文件清单

| 文件 | 消息 | TYPE | 方向 | 文档 |
|---|---|---|---|---|
| `common.schema.json` | 共享 `$defs`(非消息) | — | — | §3.6、§4、§6、§7.1 |
| `msg-hello.schema.json` | `HELLO` | `0x01` | App→设备 | §3.1 / §7.1 |
| `msg-hello_ack.schema.json` | `HELLO_ACK` | `0x81` | 设备→App | §3.1 / §7.1 |
| `msg-get_caps.schema.json` | `GET_CAPS` | `0x02` | App→设备 | §3.1 |
| `msg-caps.schema.json` | `CAPS` | `0x82` | 设备→App | §3.1 / §4 / §8 |
| `msg-ping.schema.json` | `PING` | `0x03` | 双向 | §3.1 |
| `msg-pong.schema.json` | `PONG` | `0x83` | 双向 | §3.1 |
| `msg-error.schema.json` | `ERROR` | `0x7F` | 设备→App | §3.1 / §6 |
| `msg-warn.schema.json` | `WARN` | `0x7E` | 设备→App | §3.1 / §5 / §6 |
| `msg-list_modules.schema.json` | `LIST_MODULES` | `0x10` | App→设备 | §3.2 |
| `msg-modules.schema.json` | `MODULES` | `0x90` | 设备→App | §3.2 |
| `msg-select_module.schema.json` | `SELECT_MODULE` | `0x11` | App→设备 | §3.2 |
| `msg-module_state.schema.json` | `MODULE_STATE` | `0x91` | 设备→App | §3.2 |
| `msg-start_scan.schema.json` | `START_SCAN` | `0x20` | App→设备 | §3.3 |
| `msg-scan_hit.schema.json` | `SCAN_HIT` | `0xA0` | 设备→App | §3.3 |
| `msg-scan_progress.schema.json` | `SCAN_PROGRESS` | `0xA1` | 设备→App | §3.3 |
| `msg-stop_scan.schema.json` | `STOP_SCAN` | `0x21` | App→设备 | §3.3 |
| `msg-scan_done.schema.json` | `SCAN_DONE` | `0xA2` | 设备→App | §3.3 |
| `msg-get_scan_hits.schema.json` | `GET_SCAN_HITS` | `0x23` | App→设备 | §3.3 |
| `msg-scan_hits.schema.json` | `SCAN_HITS` | `0xA4` | 设备→App | §3.3 |
| `msg-set_rssi_stream.schema.json` | `SET_RSSI_STREAM` | `0x22` | App→设备 | §3.3 |
| `msg-rssi_sample.schema.json` | `RSSI_SAMPLE` | `0xA3` | 设备→App | §3.3 |
| `msg-capture_start.schema.json` | `CAPTURE_START` | `0x30` | App→设备 | §3.4 |
| `msg-capture_done.schema.json` | `CAPTURE_DONE` | `0xB1` | 设备→App | §3.4 |
| `msg-capture_abort.schema.json` | `CAPTURE_ABORT` | `0x31` | App→设备 | §3.4 |
| `msg-tx_request.schema.json` | `TX_REQUEST` | `0x40` | App→设备 | §3.7 / §7.1 |
| `msg-tx_status.schema.json` | `TX_STATUS` | `0xC0` | 设备→App | §3.7 |
| `msg-tx_abort.schema.json` | `TX_ABORT` | `0x41` | App→设备 | §3.7 |
| `msg-save_signal.schema.json` | `SAVE_SIGNAL` | `0x42` | App→设备 | §3.7 |
| `msg-replay_signal.schema.json` | `REPLAY_SIGNAL` | `0x43` | App→设备 | §3.7 / §7.1 |
| `msg-list_saved.schema.json` | `LIST_SAVED` | `0x50` | 双向 | §3.8 |
| `msg-saved_list.schema.json` | `SAVED_LIST` | `0xD0` | 双向 | §3.8 |
| `msg-delete_saved.schema.json` | `DELETE_SAVED` | `0x51` | App→设备 | §3.8 |
| `msg-get_status.schema.json` | `GET_STATUS` | `0x52` | 双向 | §3.8 |
| `msg-status.schema.json` | `STATUS` | `0xD2` | 双向 | §3.8 |
| `msg-set_net.schema.json` | `SET_NET` | `0x53` | App→设备 | §3.8 |
| `msg-ota_begin.schema.json` | `OTA_BEGIN` | `0x54` | App→设备 | §3.8 |

共 **36 个消息 schema + 1 个公共定义文件**(`common.schema.json` 含 **29 个 `$defs`**)。
文档 §3 表格登记 37 个消息类型;减去**二进制帧** `DATA_CHUNK`(见 §2.1),覆盖率 **36/36 = 100%**,无遗漏、无多余,且每个 `title` 里的 TYPE 与文档逐一核对一致。

### 2.1 `DATA_CHUNK`(`0xB0`)为什么没有 JSON Schema

协议文档 §2.1 规定:**数据帧(`TYPE` 最高位 = 1)`PAYLOAD` 不是 JSON**,而是 §3.5 的二进制块(RLE 跳变序列)+ 尾部 JSON 元数据。
因此:

- 不存在 `msg-data_chunk.schema.json`——用 JSON Schema 描述二进制 RLE 布局是错误建模;
- `DATA_CHUNK` 尾部的**元数据 JSON 部分**定义在 [`common.schema.json`](./common.schema.json) 的 `$defs.dataChunkMeta`(`f_hz` / `mod` / `rssi_dbm` / `sample_clock_hz` / `long_gaps`),解析该二进制帧尾部的实现可以用它校验元数据;
- §3.5 的 `FLAGS` 两位(bit0 = OOK/ASK 电平序列 vs FSK 过零/相位序列;bit1 = 判决点)属于二进制头部,不在 JSON 里;bit1 的语义被 `features.raw_semantics` 镜像(§3.6 规则 4),发射方向则固定置 0(§3.7);
- 二进制布局本身以协议文档 §3.5 的文字为准,建议由 `docs/70-test/` 的黄金向量(byte-level fixture)覆盖,而不是 schema。

## 3. 如何校验

### 3.1 结构自检(本仓库自带,纯标准库)

```bash
python3 tools/validate_schemas.py
```

`tools/validate_schemas.py` 校验:JSON 合法性、必需顶层键(`$schema`/`$id`/`title`/`description`)、`$schema` 是 draft-07、`$id` 与文件名一致且唯一、`msg-*` 必须声明 `type`、带非空 `properties` 的对象必须显式声明 `additionalProperties`、每个 `$ref` 指向的本地文件存在且其 `#/$defs/...` JSON Pointer 可解析。
退出码 0 = 全部通过(当前:**0**;37 个文件、29 个 `$defs`、70 处 `$ref`)。

> **限制(重要)**:本机没有 pip、没有 `jsonschema` 等第三方库,因此该脚本**不做完整 schema 求值**(不验证 `type`/`enum`/`required`/`additionalProperties` 是否真的能接受/拒绝某份载荷),也不校验 `allOf` 的语义。
> 它是"文件与引用完整性"检查,不是"载荷合规性"检查。载荷合规性检查需要 §3.3 的工具。

### 3.2 单文件语法检查

```bash
for f in protocol/schemas/*.json; do python3 -m json.tool "$f" > /dev/null || echo "BAD: $f"; done
```

### 3.3 完整求值(需要有依赖的环境,本机不可用)

在装有 `jsonschema` 的环境(CI 或开发者机器)里:

```bash
python3 -m jsonschema -i payload.json protocol/schemas/msg-hello.schema.json
# 或
check-jsonschema --schemafile protocol/schemas/msg-hello.schema.json payload.json
```

由于所有 `$ref` 都是**相对路径**(见 §4.2),在 `protocol/schemas/` 目录下就地校验即可,不需要网络。

## 4. 编写约定

### 4.1 严格双轨(这是有意的,不要"修好"它)

| 轨道 | 规则 | 理由 |
|---|---|---|
| **开发期(本目录)** | 每个消息/嵌套对象都设 `additionalProperties: false`,并列出 `required` | 抓拼写错误与字段漂移;让文档改动在 schema diff 里显形 |
| **运行时(固件/App 解析器)** | **必须宽松忽略未知字段**,未知消息类型回 `ERROR{unsupported}` | 协议文档 §1「前向兼容」、§8「新增字段不升版本」 |

也就是说:**schema 说"未知字段不合法" ≠ 解析器可以拒绝未知字段。**
这是双轨设计,不是矛盾:`additionalProperties: false` 是给**开发和 CI 的护栏**,而线上设备与 App 必须能接受比自己更新的对端(§8 规定新增字段不升 `VER`),因此运行时**只能忽略**未知字段。
违反任一轨都有明确代价:去掉 schema 的 `additionalProperties: false` 就失去拼写错误检测;让运行时拒绝未知字段就会在固件/App 版本错配时直接崩溃,违反 §1。

同理,`required` 在 schema 里是"本版本必须出现",而运行时对**缺失**字段的宽容度由实现决定,但**不得**因为缺字段而崩溃。

> 这条双轨已经真实生效:契约在数十分钟内从 v1 涨到 v1.1——新增 2 个消息类型、把 `SCAN_DONE.hits` 拆成三个计数、`features` 增加 8 个字段、`HELLO`/`HELLO_ACK` 增加配对字段。这正是 `additionalProperties: false` 会立刻报出来的漂移类型。

### 4.2 `$id` 与 `$ref`

- 顶层 `$id` 使用**文件名本身**(如 `"msg-hello.schema.json"`),不使用 `https://...` 绝对 URL。原因:本仓库离线可用,绝对 `$id` 会把相对 `$ref` 解析成需要联网抓取的 URL。
- 跨文件引用一律用相对路径:`{"$ref": "common.schema.json#/$defs/rssi"}`。
- `common.schema.json` **内部**自引用用 `{"$ref": "#/$defs/band"}`。
- 字段级说明与 `$ref` 并存时用 `allOf` 包裹:

  ```json
  "rssi_dbm": {
    "allOf": [{ "$ref": "common.schema.json#/$defs/rssi" }],
    "description": "该点的接收信号强度,单位 dBm。"
  }
  ```

  (draft-07 中 `$ref` 的**同级关键字会被忽略**,所以不能把 `description` 与 `$ref` 平铺写在一起。)
- 反过来,**不要**把 `additionalProperties: false` 的对象放进 `allOf`:每个子 schema 都会把对方的属性视为"额外属性"而互相拒绝。因此 `SCAN_HIT`(带 `scan_id`)与 `SCAN_HITS.hits` 的元素(不带 `scan_id`,定义在 `$defs.hitPoint`)虽然同形,也是分开定义的,而不是用 `allOf` 拼装。

### 4.3 类型规则

| 语义 | Schema 写法 | 说明 |
|---|---|---|
| 频率(`*_hz`、`sample_clock_hz`) | `integer`,`minimum: 0` | 一律整数 Hz。换算提示写在 `description` 里(如 433920000 = 433.92MHz) |
| RSSI / 门限 / 噪声底(`*_dbm`) | `number`(`$defs.rssi`) | 文档 §3.6 示例带小数(-62.5) |
| 发射功率(`power_dbm`/`tx_max_dbm`/`tx_power_cap_dbm`) | `number`(`$defs.powerDbm`) | 与 RSSI 分开定义,避免把"发射功率"误标成"接收强度"。**出厂 `tx_power_cap_dbm` = 20**(2.4GHz 满功率,C-11) |
| 置信度(`*_confidence`) | `["number","null"]`,`0.0–1.0`(`$defs.confidence`) | §3.6 规则 1:取值 0.0–1.0 **或 null**;null ≠ 字段缺失 |
| 猜测量(可为 null) | `$defs.guessString` / `$defs.guessInt` | §3.6 规则 2:证据不足时必须置 `null` |
| 时间(`*_ms`) | `number` | 文档 §3.6 `duration_ms = 41.2` 证明存在小数;`number` 同时接受整数 |
| 计数量(`hits*`/`edges`/`repeats`/`*_bits`/`max_edges` …) | `integer`,`minimum` | 与时间字段区分开 |
| 字节数(`ble_frame_max`) | `integer`,`1..4096` | 4096 是 §2 的 `LEN` **格式层逻辑上限**;**BLE 上真实上界 3669** 且随协商变化(见 §7)。二者不可混用 |
| 百分比(`pct`) | `number`,`0..100` | 文档未给范围,按"百分比"语义取 0..100(判断,见 §7) |
| 空载荷消息 | `type: object` + `additionalProperties: false`,无 `properties` | 文档写 `{}` 的消息 |
| 原始码流(`payload_rle`) | `string`(base64url) | 契约 §3.7 规定:RLE 结构在 JSON 消息里以 **base64url 字符串**承载(而非数组/对象),与 `DATA_CHUNK` 的二进制布局同构 |

### 4.4 `required` 的判定规则

协议文档**从未标注哪些字段可选**。本目录采用的规则是:

1. 文档在载荷里**列出的字段一律进 `required`**;
2. 例外一:文档自身写明**条件性/场景性**的字段——只定义、不 `required`:

   | 字段 | 条件 | 出处 |
   |---|---|---|
   | `TX_REQUEST.auth_token` | 仅当 `policy.tx_requires_token` 为真时必需 | §4、§7 |
   | `REPLAY_SIGNAL.auth_token` | 同上 | §7 |
   | `TX_REQUEST.armed_ack` | 仅两段式发射的第二段出现 | §3.7 |
   | `TX_STATUS.code` / `TX_STATUS.detail` | 仅 `denied`/`failed` 时有意义 | §3.7(未明说,判断) |
   | `TX_STATUS.granted_power_dbm` | **`armed`/`sent`/`failed` 时必须给出;`denied` 时可省略**(未发射即无"生效值") | §3.7(2026-09-21 新增明文) |
   | `MODULE_STATE.detail` | 仅 `error` 等状态需要 | §3.2(未明说,判断) |
   | `HELLO.pairing` / `HELLO.pair_code` | 仅首次配对握手出现。⚠️ **本批 `pair_code` 无活路径**(USB 不校验、BLE 在取码前即回 `no_display`)—— 字段保留但**不要为它实现校验分支** | §3.1「配对时另带」、§7.1(1) |
   | `HELLO_ACK.pair_ok` / `first_bond` / `pairing_secret` / `pair_error` | 仅配对握手出现。⚠️ `pairing_secret` **仅 USB 链路**(2026-09-21 定稿,原为「BLE 或 USB」) | §7.1(1)(1.1) |
   | `HELLO_ACK.last_accepted_seq` | **文档 §7.1(1.2) 称其为"必须字段"**,但 schema 按协调者决策登记为可选(向后兼容) | §7.1(1.2) —— 张力见 §7.1 第 2 条 |
   | `CAPS.deprecated` | 仅存在弃用字段时出现 | §8 |
   | `features` 的 8 个 v1.1 字段 | 见下条规则 | §3.6 规则 5 |

3. 例外二:`features.required` **只含 v1 的 14 个字段**(`f_hz` `mod` `rssi_dbm` `edge_count` `duration_ms` `symbol_rate_bps` `pulse_width_us` `encoding_guess` `repetition_ms` `payload_bits` `preamble_bits` `crc_guess` `frame_repeats` `noise_floor_dbm`)。
   依据是 §3.6 **规则 5**:v1.1 新增字段(`raw_semantics` / `*_confidence` / `incomplete`)**可能因固件较旧而缺失**,缺失时一律按"未知/低"处理。
   若把它们设为必填,规则 5 就永远不可能触发(自我否定),并且旧固件报文会被 CI 误判非法——而 §8"新增字段不升版本"的前提恰恰是旧端报文仍然合法。
   "设备必须真的产出置信度"这条约束由**测试**把关(如 TC-034c),不靠 schema 必填。

若协议文档后续补齐"可选/必填"标注,应同步修正本表与各 schema 的 `required`。

### 4.5 `common.schema.json` 的 29 个 `$defs`

| `$defs` | 用途 |
|---|---|
| `frequencyHz` | 整数 Hz 频率 |
| `rssi` / `powerDbm` | dBm 电平(接收 / 发射,分开定义) |
| `confidence` | 0.0–1.0 或 null 的置信度(§3.6 规则 1) |
| `guessString` / `guessInt` | 可为 null 的猜测量(§3.6 规则 2) |
| `slot` / `scanId` | 模块槽位 1..3 / 扫描会话标识 |
| `pairError` | 配对失败原因(§7.1(1.1)) |
| `errorCode` / `warnCode` | §6 错误码 / WARN 码 |
| `mod` / `txMod` | 接收侧调制(含 `auto`)/ 发射侧调制(不含 `auto`,§3.7) |
| `rawSemantics` / `moduleState` / `txState` / `scanMode` | 各枚举 |
| `band` / `bands` / `freqRange` | 频段(数组形式与命名形式) |
| `hitPoint` | `SCAN_HITS.hits` 元素 |
| `spi` | SPI 引脚分配(单 SPI2 总线 + 每模块独立 CS) |
| `pulseWidthUs` / `features` | §3.6 特征 |
| `moduleInfo` / `capsModule` | §3.2 MODULES 元素 / §4 CAPS.modules 元素 |
| `policy` / `limits` | §4 发射策略与设备限制 |
| `dataChunkMeta` | §3.5 二进制帧尾部元数据 |

## 5. 文档改动时,schema 必须同步

规则(与 `docs/README.md` 的"我要改协议"路线一致):

> **任何字段改动,先改 `docs/30-protocol/010-device-app-protocol.md` 并升版本号,然后同一批改动里同步 `protocol/schemas/`,再通知固件与 App 两侧。**

合并前自检清单:

1. [ ] `sha256sum docs/30-protocol/010-device-app-protocol.md` 与本 README 顶部记录的值比对;不一致说明文档变了,逐条核对下列各项;
2. [ ] 文档里新增/改名/删除的字段,已同步到对应 `msg-*.schema.json`(字段名逐字符一致);
3. [ ] 新增枚举值已同步进 `common.schema.json` 的 `enum`(错误码/WARN 码见 §6,状态见 §3.2/§3.7,调制见 §3.4/§3.7,配对失败见 §7.1(1.1));
4. [ ] 文档新增消息类型 → 新增 `msg-<type小写下划线>.schema.json`(小写、下划线),并更新本 README 的 §2 表格;
5. [ ] 文档删除字段 → 按 §8 的流程先在 `CAPS.deprecated` 标记再删;schema 侧同步;
6. [ ] **检查 §3.6 规则 5 是否仍允许 v1.1 字段缺失**:若契约改为"必须提供",则把 `features` 的 8 个 v1.1 字段移入 `required`;
7. [ ] 更新本 README 顶部的镜像 sha256 与 §7「已知文档缺口」;
8. [ ] `python3 tools/validate_schemas.py` 退出码 0;
9. [ ] `for f in protocol/schemas/*.json; do python3 -m json.tool "$f" >/dev/null || echo BAD $f; done` 无输出。

**版本**:`VER`(`0x01`)只在**帧格式**变化时升(§8);新增消息类型/新增字段**不升版本**。
因此 schema 不单独维护语义版本号,由"镜像的文档 sha256 + git 历史"体现。
注意 v1.1 就属于"不升 `VER`"的改动:新增 2 个消息类型、**删除** `SCAN_DONE.hits`(§8 已说明这是破坏性改名,因无已发布固件而直接改名)、`features` 增字段——**设备与 App 必须按字段而不是按版本号适配**。

## 6. 枚举来源对照

| 枚举 | 取值 | 文档出处 |
|---|---|---|
| `$defs.errorCode` | `not_ready` `version_mismatch` `module_absent` `module_busy` `band_out_of_range` `tx_denied` `tx_bad_token` `frag_timeout` `frame_too_large` `arm_timeout` `hw_error` `unsupported` | §6(非 WARN 的 12 项,与表格逐项一致) |
| `$defs.warnCode` | `band_clipped` `throttled` `edge_clamped` `preempted` | §6 标注 `(WARN)` 的 3 项 + §5 规则 3 正文的 `preempted`(**§6 表未收录,见 §7**) |
| `$defs.mod` | `ook` `2fsk` `auto` | §3.4(`CAPTURE_START`,接收侧) |
| `$defs.txMod` | `ook` `2fsk` | §3.7(载荷行已明写"不含 auto") |
| `$defs.rawSemantics` | `undecided` `decided` `unknown` | §3.5 `FLAGS` bit1 / §3.6 规则 4 |
| `$defs.pairError` | `bad_code` `no_display` `already_bound` `ble_only` `rng_failed` | §7.1(1.1);`no_display` 为 2026-09-21 新增(OLED 延期,见下) |
| `$defs.moduleState` | `ready` `busy` `absent` `error` | §3.2 |
| `$defs.txState` | `denied` `armed` `sent` `failed` | §3.7 |
| `$defs.scanMode` | `peak` `sweep` | §3.3 |
| `moduleInfo.ant` | `SMA` | §3.2(字面单值) |
| `hello_ack.chip` | `esp32s3` | §3.1(字面单值) |

## 7. 已知文档缺口、不一致与判断(只报告,未擅自"修好")

### 7.1 文档内部矛盾 / 不一致

1. **`§3.2 MODULES` 与 `§4 CAPS.modules` 的元素字段不一致。**
   §3.2 为 `{slot, chip, present, bands, tx_max_dbm, driver_version, spi:{...}, ant:"SMA"}`,§4 为 `{slot, chip, present, bands, tx_max_dbm, tx_allowed}`:
   `driver_version`/`spi`/`ant` 只在 §3.2 出现,`tx_allowed` 只在 §4 出现。
   当前处理:分别定义 `$defs.moduleInfo` 与 `$defs.capsModule`,两边都不擅自补齐。**这是最实质的一处不一致。**
2. **`HELLO_ACK.last_accepted_seq`:文档说"必须",schema 是"可选"。**
   §7.1(1.2) 明写"是**必须字段**",而本 schema 按协调者决策把它登记为**可选**(理由:避免旧固件报文被 `additionalProperties`/`required` 判非法,与 §3.6 规则 5 的向后兼容思路一致)。
   这是一个**有意保留的口径差异**:若契约要强校验,应把它移入 `required`(一行改动),否则请把 §7.1(1.2) 的"必须"改为"SHOULD"。
3. **`auth_token` 的时效表述自相矛盾。**
   §7 安全表写"HMAC-SHA256,**30s 有效**";而 §7.1(3) 校验规则第 4 条明确设备**不应**判断令牌年龄,"正确实现里**根本不存在'过期'这个概念**,只有 `seq` 单调性",并把"按 30 秒过期实现"列为实现陷阱。
   当前处理:schema 不做时效约束(只声明为 string),description 按 §7.1(3) 描述并在 README 记录冲突。**建议删掉 §7 表格里的"30s 有效"。**
4. **`payload_rle` 的承载者写多了一个消息。**
   §3.7 的编码小节说"在 **JSON 消息**(`TX_REQUEST` / `REPLAY_SIGNAL`)里,该结构以 base64url 字符串承载",但 §3.7 的 `REPLAY_SIGNAL` 载荷是 `{name, repeats, auth_token}`,**没有任何承载该结构的字段**。
   当前处理:不擅自给 `REPLAY_SIGNAL` 加字段,按载荷清单建模。请确认是笔误(应为 `TX_REQUEST` + `SAVE_SIGNAL`?)还是 `REPLAY_SIGNAL` 需要补字段。
5. **§3.6 的表格与其 JSON 示例仍对不上。**
   表格说估计量(`symbol_rate_bps` `repetition_ms`)与猜测量(`encoding_guess` `payload_bits` `preamble_bits` `crc_guess`)都"必带同名 `*_confidence`";示例里只有 `symbol_rate_confidence`、`encoding_confidence`、`payload_bits_confidence`、`crc_confidence`——**缺 `repetition_ms_confidence` 与 `preamble_bits_confidence`**。
   当前处理:两者都定义,但**不进 `required`**(与其它 v1.1 字段一致);若契约认为它们必填,应同时补进示例。
   另:"同名 `*_confidence`"的命名不严格——`encoding_guess` 对应的是 `encoding_confidence`,不是 `encoding_guess_confidence`;请实现以示例为准。
6. **§3.6 规则 2 的措辞覆盖不到 `payload_bits`/`preamble_bits`。**
   规则 2 说"任何 `*_guess` 类字段允许为 `null`",而表格把 `payload_bits`/`preamble_bits` 也列为"猜测量……必须把值置 `null`",二者后缀并不匹配。
   当前处理:四个猜测量(`encoding_guess` `payload_bits` `preamble_bits` `crc_guess`)**全部设为可空**。
7. ~~**`WARN{preempted}` 只在 §5 规则 3,§6 表里没有。**~~ ✅ **已关闭(契约 v1.1)**:§6 表已补 `preempted`(WARN)行;`$defs.warnCode` 四值与 §6 一致。
8. ~~**§2.2.1 的分片预算推导有一处算术/口径滑移。**~~ ✅ **已关闭(契约 v1.1)。但本条的原始报告本身有两处错误,记此以免以后再算错:**
   - 原文 `247 − 1 − 1 = 241` **确实写错**,正确为 **245**;契约已更正。
   - 但本条据以推出的"正确链"(`244B/片 → 3660B → LEN 上限 3654B`)**同样不对**,因为它依据的是"§2.3 明确 `FRAG 头` 是 2B"——而 §2.3 的 `[FRAG_IDX:4bit | FRAG_TOTAL:4bit]` 两字段各占 4bit,**合计恰好 1B**。那里写"2B"是原文自相矛盾的笔误,契约已一并修正为 1B。
   - **最终口径**:`ATT 载荷 247 − (ATT 操作码 1 + FRAG 头 1) = 每片净载 245B`;`× 15 = 3675B`(整帧上限);`− 帧头 6B = LEN 上限 3669B`。与 `4096` 差 **427B**,故"原契约在 BLE 上数学不可实现"成立。
9. ~~**§3.8 的 `OTA_BEGIN` 交叉引用写错**~~ ✅ **已关闭**:契约已改为"预留,见 §9 非目标"。
10. ~~**`arm_timeout_ms` 被引用却无处承载。**~~ ✅ **已关闭(契约 v1.1)**:已加入 `CAPS.limits.arm_timeout_ms`(`integer`,并进入 `limits.required`);契约 §4 的 `limits` 表与示例同步。

### 7.2 文档缺失(载荷未定义,无法严格闭合)

| # | 位置 | 缺什么 | 当前处理 |
|---|---|---|---|
| 1 | `§3.8 SAVED_LIST` / `LIST_SAVED` | 清单元素的字段;请求是否有参数 | `LIST_SAVED` = `{}`(仿 §3.1/§3.2 的空请求);`SAVED_LIST` = **开放对象数组**(元素不设 `additionalProperties:false`,否则会拒绝一切真实载荷) |
| 2 | `§3.8 STATUS` / `GET_STATUS` | 温度/供电/Wi-Fi RSSI/模块状态的**字段名** | `GET_STATUS` = `{}`;`STATUS` = **开放对象**。**这是本次唯一无法严格闭合建模的消息** |
| 3 | `§3.7 TX_STATUS.code` | 取值空间(已知值:`arm_timeout`、`tx_denied`、`tx_bad_token`,但没有表) | `string`,不设 `enum` |
| 4 | `§3.7 SAVE_SIGNAL.raw_ref` | 类型 | `string`(判断) |
| 5 | `§3.8 OTA_BEGIN` | 载荷(标"预留",§9 说本期不做) | 空对象 `{}` |
| 6 | `§3.1 PING/PONG.t_ms` | 是回显 `PING.t_ms` 还是设备本地时刻(§3.6 时间基准表只覆盖 `SCAN_HIT`/`RSSI_SAMPLE` 的 `t_ms`) | `number`,在 description 标注歧义 |
| 7 | `§3.4/§3.5 scan_id` | 类型、起始值、是否跨扫描复用 | `$defs.scanId` = 非负整数(判断) |
| 8 | `§3.3 GET_SCAN_HITS` | `offset` 是否 0 起始、`limit` 上限、越界/过期 `scan_id` 的行为与错误码 | `offset` ≥ 0、`limit` ≥ 1,不设上限 |
| 9 | `§3.3 SCAN_HITS.total` | 语义(该 `scan_id` 的总命中数,还是本页条数) | 按分页惯例取前者 |
| 10 | `§4 policy.tx_whitelist` | 元素类型(示例是空数组) | 按 §1"频段白名单"与 §6 `band_out_of_range` 推断为 `band[]` |
| 11 | `§3.3 SCAN_PROGRESS.pct` | 范围 | 按百分比取 `0..100`(判断) |
| 12 | `§3.8 SET_NET.pass` | 长度/编码约束(§7 只要求走 BLE) | `string`,不设 `minLength`(避免误拒) |
| 13 | 全文档 | **没有"必填/可选"标注** | 见 §4.4 的判定规则 |

### 7.3 schema 表达不了的约束(需要实现或测试兜底,不是 schema 缺陷)

- `SCAN_DONE`:不变量 `hits_dropped = hits_total − hits_pushed`,draft-07 无法做跨字段算术;且 `hits_dropped > 0` 时 App 必须提示"明细不完整"(§3.3)。
- `CAPTURE_DONE.truncated = true` ⇒ `features.incomplete` 应为 `true`(§3.6 规则 3),这是跨报文/跨字段的条件。
- `CAPTURE_START.max_edges > CAPS.limits.max_edges` ⇒ 设备钳制并发 `WARN{edge_clamped}`(§3.4)。
- `policy.tx_whitelist` 为空 ⇒ 禁止一切发射(§4);`tx_requires_token` ⇒ `auth_token` 必需。
- `TX_REQUEST.f_hz` 必须落在 `policy.tx_whitelist` 内,且 `power_dbm ≤ min(policy.tx_power_cap_dbm, 模块 tx_max_dbm)`(§6、§7.1)。
- `auth_token` 与 `device_id | seq_be32 | f_hz | mod | power_dbm×10` 的绑定、一次性有效、常数时间比较、`seq` 严格递增(§7.1(2)(3));`tx_bad_token` 的恢复流程必须先重读 `last_accepted_seq`(§7.1(3.1))。
- `ble_frame_max` 是**承载相关**的(BLE 连接下有效,Wi-Fi 下不代表上限,§4),schema 无法表达"按传输通道变化"。
- 命中的 `f_hz` 是否落在某模块 `bands` 内(§6 `band_out_of_range` 的应对)。

### 7.4 已按契约/协调者决策定稿、不再算缺口的三处

| 项 | 结论 |
|---|---|
| `TX_REQUEST.mod` 取值集合 | **已关闭**:§3.7 载荷行现明写 `mod:"ook"\|"2fsk"`,并说明不含 `auto`(接收侧语义)。schema 用 `$defs.txMod` 二值 |
| §6 错误码表混装 WARN 码 | **已关闭**:`frame_too_large`/`arm_timeout` 归 ERROR,`edge_clamped` 归 WARN;schema 拆成 `errorCode`/`warnCode` 两个枚举 |
| `features` 缺不确定性表达 | **已关闭**:§3.6 规定三级不确定性;schema 侧 `$defs.confidence`(可空)、`$defs.guessString`/`guessInt`、`$defs.rawSemantics`(三值)与 `incomplete`;`required` 仍为 v1 的 14 项(§3.6 规则 5) |

### 7.5 可同步但我没有擅自动(等契约方确认)

1. **`t_ms` 的语义**:§3.6 新增的时间基准表明确 `SCAN_HIT`/`RSSI_SAMPLE` 的 `t_ms` 是设备**单调 `uptime_ms`**(不是绝对时间,跨重启不可比较)。schema 目前只写"单位 ms"。要不要把该语义补进这两个字段的 `description`?(纯文案,不影响校验)
2. **`payload_rle` 收紧为 `"type": "string"`**:§3.7 已定义它是 base64url 文本,收紧后能挡住传数组/对象的误用。当前保持不约束(更宽),等确认。
3. **`SCAN_HIT`/`RSSI_SAMPLE` 的 `t_ms` 若改为 `$defs.uptimeMs`**:可把"单调 uptime"语义一次定义、多处引用。

## 8. 与通用约定的偏差(有意的)

除了 `common.schema.json` 与 `DATA_CHUNK` 之外,以下消息的 schema **根类型是 `array` 而不是 `object`**,因为协议文档 §3.2/§3.8 的载荷字面就是数组:

| 文件 | 文档原文 |
|---|---|
| `msg-modules.schema.json` | §3.2:`[{slot, chip, present, bands, ...}]` |
| `msg-saved_list.schema.json` | §3.8:"已存信号清单"(元素结构未定义) |

"每个 schema 文件顶层是 object"指的是**文件根是 JSON 对象**(携带 `$schema`/`$id`/`title`/`description`),这一点对全部 37 个文件成立;而**载荷本身的 `type`** 必须与文档一致,故上述两个文件为 `array`。二者冲突时以文档为准。

同理,`STATUS` 与 `SAVED_LIST` 的元素**没有** `additionalProperties: false`:文档没给字段名,闭合会拒绝一切合法载荷。这是"文档未定义"导致的例外,已在 §7.2 登记;`tools/validate_schemas.py` 的"带非空 properties 必须声明 additionalProperties"检查对它们自然通过(它们没有声明任何属性)。

## 9. 变更历史

| 日期 | 变更 |
|---|---|
| 2026-09-20 | 首版:覆盖协议 v1 §3 全部 34 个消息类型 + `common.schema.json` 共享定义;随附 `tools/validate_schemas.py` |
| 2026-09-20 | **v1.1 同步**:新增 `msg-get_scan_hits`(0x23)/`msg-scan_hits`(0xA4);`SCAN_HIT` 加 `scan_id`;`SCAN_DONE` 改为 `hits_total`/`hits_pushed`/`hits_dropped`(删除旧 `hits`);`common` 新增 `scanId`/`hitPoint`/`confidence`/`rawSemantics`/`txMod`,`features` 按 §3.6 重写,`limits` 增 `ble_frame_max`(1..4096),`errorCode` 增 `frame_too_large`,`warnCode` 增 `edge_clamped`;`HELLO.pair_code`/`HELLO_ACK.pair_ok`/`CAPS.deprecated` 补为可选 |
| 2026-09-20 | **v1.1 收口**(镜像 `1afe4f02…`,按协调者定稿 delta):`errorCode` 增 `arm_timeout`(12 项);`rawSemantics` 增第三值 `unknown`;新增 `$defs.pairError`;`msg-hello` 增可选 `pairing`+`pair_code`;`msg-hello_ack` 增 `last_accepted_seq` 与配对字段(`pair_ok`/`first_bond`/`pairing_secret`/`pair_error`,均可选);`msg-tx_request` 增可选 `armed_ack`,`txMod` 描述按 §3.7 正文改写;`confidence` 改为可空(`["number","null"]`);**`features.required` 收缩为 v1 的 14 项**(§3.6 规则 5)。共 36 消息 + 29 `$defs`。校验:`python3 tools/validate_schemas.py` 退出码 **0** |
| 2026-09-21 | **NQ-01(A 方案)+ NQ-03 的镜像同步**(镜像 `9659d6be…`):`msg-tx_status` 新增**可选** `granted_power_dbm`(`$ref` → `$defs.powerDbm`);`msg-hello` / `msg-hello_ack` 的配对字段语义改为「配对只走 USB、`pair_code` 不校验」;**未新增/删除消息,未改帧格式**,`VER` 不变,仍为 36 消息 + 30 `$defs`。校验:`python3 tools/validate_schemas.py` 退出码 **0** |
| 2026-09-21 | **C-11 + OLED 延期的镜像同步**(镜像 `d33d5e8b…`;该值随契约文档头日期同步更新,见上):`$defs.pairError` 增第 5 值 **`no_display`**;`powerDbm` / `policy.tx_power_cap_dbm` 的描述改为出厂 **20**(原 10);`$defs.powerDbm` 与 README 的枚举对照表同步。**未新增/删除消息,未改帧格式**,故 `VER` 不变,仍为 36 消息 + 30 `$defs`。校验:`python3 tools/validate_schemas.py` 退出码 **0** |
| 2026-09-21 | **取消无人值守的镜像同步**(镜像 `df9b4929…`):**本次 schema 无任何改动** —— 契约侧只是把"设备侧离线记录/回读"从待决项写成 **§9 非目标**(该能力从未定义过消息)。仅重 pin 文档 sha256。仍为 36 消息 + 30 `$defs`。校验:`python3 tools/validate_schemas.py` 退出码 **0** |
