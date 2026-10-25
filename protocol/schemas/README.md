# RF-Lab 协议 JSON Schema(机器可读镜像)

状态:`draft` · 契约版本 `v1.1` · 镜像契约文档:[`docs/04-protocol.md`](../../docs/04-protocol.md) · 契约文档 sha256:`eb8aa44235dbdacdbbd19ae6dccce793b9acb5a42ee2200aa8acd6760e40ba24`

> **权威关系**:[`docs/04-protocol.md`](../../docs/04-protocol.md) 是唯一权威来源;本目录只是它的**机器可读镜像**,用于开发期与 CI 校验。**schema 与契约冲突时,以契约为准,并立刻修正 schema。**
> 契约文档定稿后,主控会计算 `sha256sum docs/04-protocol.md` 并回填上面的 `<待更新>` 占位;不一致即说明契约已变更,按 §3 逐条同步。

## 1. 用途与作用范围

- 给固件(设备侧序列化)与 App(解析)提供可编程的载荷契约,在**开发期/测试期**抓出字段拼写错误、类型错误、枚举越界;
- 给 AI 提示词与测试用例([`docs/06-test-plan.md`](../../docs/06-test-plan.md))提供字段清单的单一事实来源;
- 契约评审时,用 `git diff protocol/schemas/` 直观看到"这次改动到底动了哪些字段"。

**作用范围**:只描述**帧头之后的 PAYLOAD**(契约 §2.1、§3)。帧头(`MAGIC/VER/TYPE/SEQ/LEN`)、BLE 分片头、以及 `ble_frame_max` 背后的分片预算推导(§2.2.1、§2.3)**不在本目录覆盖范围内**,以契约文字为准。

## 2. 文件清单

| 文件 | 消息 | TYPE | 方向 | 契约节 |
|---|---|---|---|---|
| `common.schema.json` | 共享 `$defs`(非消息) | — | — | §3.5、§3.6、§4、§6、§7.1 |
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

共 **36 个消息 schema + 1 个公共定义文件**(`common.schema.json` 含 **30 个 `$defs`**)。契约 §3 登记 37 个消息类型,减去**二进制帧** `DATA_CHUNK`(见 §2.1)后覆盖率 **36/36 = 100%**,无遗漏、无多余;每个 schema 的 `title` 里的 TYPE 与契约逐一核对一致。

## 3. 二进制布局:`DATA_CHUNK`(`0xB0`)为什么没有 JSON Schema

契约 §2.1 规定**数据帧(`TYPE` 最高位 = 1)的 `PAYLOAD` 不是 JSON**,而是 §3.5 的二进制块(RLE 跳变序列)+ 尾部 JSON 元数据。因此不存在 `msg-data_chunk.schema.json`——用 JSON Schema 描述二进制 RLE 布局是错误建模。注意事项:

- **二进制尾部**:布局为 `FLAGS(1B)` + `edge[0]` 起点(`u32 LE` 相对微秒)+ RLE 对(`u16 LE dt_us` + `u8 level`)+ `0x00 0x00` 分隔 + UTF-8 JSON 元数据。字段顺序与字节序**以契约 §3.5 为准**,不要凭 schema 反推;
- **尾部元数据**可用 [`common.schema.json`](./common.schema.json) 的 `$defs.dataChunkMeta`(`f_hz` / `mod` / `rssi_dbm` / `sample_clock_hz` / `long_gaps`)校验;
- **`FLAGS` 两位是二进制头部,不在 JSON 里**:bit0 = OOK/ASK 电平序列 vs FSK 过零/相位序列;bit1 = 判决点(0 = 未经判决的模拟前端序列,保留时序;1 = 芯片已判决位流,时序已丢失)。bit1 的语义被 `features.raw_semantics` 镜像(契约 §3.6 规则 4),发射方向固定置 0(契约 §3.7);
- **`dt_us` 饱和值 `0xFFFF`** 表示超长间隔,真实值按顺序落在 `long_gaps[]` 里——解析器不得把饱和值当真实间隔;
- 布局本身由 [`docs/06-test-plan.md`](../../docs/06-test-plan.md) 的黄金向量(byte-level fixture)覆盖,而不是由 schema 表达。

## 4. 字段变更规则

> **任何字段改动,先改 [`docs/04-protocol.md`](../../docs/04-protocol.md)(必要时升版本),然后在同一批改动里同步本目录,再通知固件与 App 两侧。**

合并前自检清单:

1. [ ] `sha256sum docs/04-protocol.md` 与本文顶部记录值比对;不一致说明契约已变更,逐条核对下列各项;
2. [ ] 契约新增/改名/删除的字段已同步到对应 `msg-*.schema.json`(字段名逐字符一致);
3. [ ] 新增枚举值已同步进 `common.schema.json` 的 `enum`(错误码/WARN 码见**契约** §6,状态见契约 §3.2/§3.7,调制见契约 §3.4/§3.7,配对失败见契约 §7.1(1.1));
4. [ ] 契约新增消息类型 → 新增 `msg-<类型小写下划线>.schema.json`,并更新本文 §2 表格;
5. [ ] 契约删除字段 → 按**契约** §8 的流程先在 `CAPS.deprecated` 标记再删;schema 侧同步;
6. [ ] `features` 的 v1.1 字段仍**不**进 `required`(理由:契约 §3.6 规则 5"缺失 = 未知");若契约改为"必须提供",才移入 `required`;
7. [ ] 更新本文顶部的契约 sha256;
8. [ ] `python3 tools/validate_schemas.py` 退出码 0;
9. [ ] `for f in protocol/schemas/*.json; do python3 -m json.tool "$f" >/dev/null || echo BAD $f; done` 无输出。

**版本口径**:`VER`(`0x01`)只在**帧格式**变化时升(契约 §8),新增消息类型/新增字段**不升版本**。因此 schema 不单独维护语义版本号,由"镜像契约的 sha256 + git 历史"体现;设备与 App 必须**按字段而不是按版本号**适配。

## 5. 校验命令

```bash
# 1) 结构自检(仓库自带,纯标准库):JSON 合法性、顶层键、draft-07、$id 与文件名一致且唯一、
#    msg-* 声明 type、带非空 properties 的对象必须显式声明 additionalProperties、
#    每个 $ref 指向的本地文件存在且 #/$defs/... 可解析
python3 tools/validate_schemas.py            # 退出码 0 = 全部通过

# 2) 单文件语法检查
for f in protocol/schemas/*.json; do python3 -m json.tool "$f" > /dev/null || echo "BAD: $f"; done

# 3) 完整 schema 求值(需要 jsonschema / check-jsonschema,本机无 pip,不可用)
python3 -m jsonschema -i payload.json protocol/schemas/msg-hello.schema.json
check-jsonschema --schemafile protocol/schemas/msg-hello.schema.json payload.json
```

当前状态:37 个文件、30 个 `$defs`、73 处 `$ref`,结构自检通过。

> ⚠️ **限制**:本机没有 `pip`、没有 `jsonschema` 等第三方库,`tools/validate_schemas.py` **只做文件与引用完整性检查**,**不做完整 schema 求值**(不验证 `type`/`enum`/`required`/`additionalProperties` 是否真的接受或拒绝某份载荷,也不校验 `allOf` 语义)。载荷合规性必须用上面的第 3 组命令在 CI 或开发者机器上跑。由于所有 `$ref` 都是相对路径(见 §6.2),在 `protocol/schemas/` 目录下就地校验即可,不需要网络。

## 6. 编写约定

### 6.1 严格双轨(有意的,不要"修好"它)

| 轨道 | 规则 | 理由 |
|---|---|---|
| **开发期(本目录)** | 每个消息/嵌套对象都设 `additionalProperties: false` 并列出 `required` | 抓拼写错误与字段漂移,让契约改动在 schema diff 里显形 |
| **运行时(固件/App 解析器)** | **必须宽松忽略未知字段**;未知消息类型回 `ERROR{unsupported}` | 契约 §1「前向兼容」、§8「新增字段不升版本」 |

也就是说:**schema 说"未知字段不合法" ≠ 解析器可以拒绝未知字段。** 去掉 schema 的 `additionalProperties: false` 会失去拼写错误检测;让运行时拒绝未知字段,会在固件/App 版本错配时直接崩溃。

### 6.2 `$id` 与 `$ref`

- 顶层 `$id` 用**文件名本身**(如 `"msg-hello.schema.json"`),不用 `https://...` 绝对 URL——本仓库离线可用,绝对 `$id` 会把相对 `$ref` 解析成需要联网抓取的 URL;
- 跨文件引用一律相对路径:`{"$ref": "common.schema.json#/$defs/rssi"}`;`common.schema.json` 内部自引用用 `{"$ref": "#/$defs/band"}`;
- 字段说明与 `$ref` 并存时用 `allOf` 包裹(draft-07 中 `$ref` 的同级关键字会被忽略);
- **不要**把 `additionalProperties: false` 的对象放进 `allOf`——每个子 schema 会把对方的属性视为"额外属性"而互相拒绝。因此 `SCAN_HIT`(带 `scan_id`)与 `SCAN_HITS.hits` 的元素(`$defs.hitPoint`,不带 `scan_id`)虽然同形也分开定义。

### 6.3 类型与 `required`

- 频率一律 `integer` Hz;RSSI/功率用 `number`(dBm,可带小数,分 `rssi` 与 `powerDbm` 两个定义);时间 `*_ms` 用 `number`;计数量用 `integer`;
- `*_confidence` 为 `["number","null"]`(0.0–1.0 或 null);猜测量用 `guessString`/`guessInt`(可 null)——契约 §3.6 规则 1/2;
- 空载荷消息(契约写 `{}`)为 `type: object` + `additionalProperties: false` 且无 `properties`;
- `payload_rle` 为 `string`(base64url,契约 §3.7);
- `required` 判定:契约列出的字段一律进 `required`;**例外**是契约自身写明条件性的字段(如 `TX_REQUEST.auth_token` 仅在 `tx_requires_token` 为真时必需、`TX_STATUS.granted_power_dbm` 在 `armed`/`sent`/`failed` 时必须给出),以及 `features` 的 v1.1 字段(理由见 §4 第 6 条)。

## 7. 与契约的已知口径差异(只报告,未擅自改 schema)

1. **`MODULES` 与 `CAPS.modules` 的元素字段不一致**:契约 §3.2 为 `{slot, chip, present, bands, tx_max_dbm, driver_version, spi, ant}`,§4 为 `{slot, chip, present, bands, tx_max_dbm, tx_allowed}`。schema 分别定义 `$defs.moduleInfo` 与 `$defs.capsModule`,**两边都不擅自补齐**。
2. **`HELLO_ACK.last_accepted_seq` 的强制力**:契约 §7.1(1.2) 表述为"设备**应**回传"(SHOULD),schema 登记为**可选**,两者口径一致但强度弱于"必须"。若契约要强校验,把它移入 `required` 即可(一行改动)。
3. **`STATUS` / `SAVED_LIST` 的元素结构未定义**(契约 §9.1 已登记):因此这两个消息为**开放对象**,元素不加 `additionalProperties: false`,否则会拒绝一切真实载荷。
4. **`features` 的置信度字段不满足"必带"的强表述**:契约 §3.6 要求估计量(含 `repetition_ms_confidence`)与猜测量(含 `preamble_bits_confidence`)必带同名置信度,但 schema 的 `required` 只含 v1 的 14 个字段(理由见 §4 第 6 条)。"设备必须真的产出置信度"由测试把关,不靠 schema 必填。
5. **schema 表达不了的约束**由实现与测试兜底,不是 schema 缺陷:`hits_dropped = hits_total − hits_pushed`;`truncated = true ⇒ features.incomplete = true`;`max_edges` 钳制并发 `WARN{edge_clamped}`;`tx_whitelist` 为空 ⇒ 禁止一切发射;`auth_token` 的参数绑定与 `seq` 严格递增;`ble_frame_max` 随承载变化。
