# RF-Lab 协议 JSON Schema(机器可读镜像)

状态:`draft` · 契约版本 `v1.1` · 镜像契约文档:[`docs/04-protocol.md`](../../docs/04-protocol.md) · 契约文档 sha256:`a70517f3c5a857341d37b83293f68e606eaf0af9353d0e27867f3ab337d89c05`

> **权威关系**:[`docs/04-protocol.md`](../../docs/04-protocol.md) 是唯一权威来源;本目录只是它的**机器可读镜像**,用于开发期与 CI 校验。**schema 与契约冲突时,以契约为准,并立刻修正 schema。**
> **契约变更流程**:改 `docs/04-protocol.md` 后必须重算并更新本文顶部的 sha256。`python3 tools/validate_schemas.py` 会自动比对它,不一致即 FAIL 并提示按 §4 同步。

## 1. 用途与作用范围

- 给固件(设备侧序列化)与 App(解析)提供可编程的载荷契约,在**开发期/测试期**抓出字段拼写错误、类型错误、枚举越界;
- 给 AI 提示词与测试用例([`docs/06-test-plan.md`](../../docs/06-test-plan.md))提供字段清单的单一事实来源;
- 契约评审时,用 `git diff protocol/schemas/` 直观看到"这次改动到底动了哪些字段"。

**作用范围**:只描述**帧头之后的 PAYLOAD**(契约 §2.1、§3)。帧头(`MAGIC/VER/TYPE/SEQ/LEN`)、BLE 分片头、以及 `ble_frame_max` 背后的分片预算推导(§2.2.1、§2.3)**不在本目录覆盖范围内**,以契约文字为准。`DATA_CHUNK`(`0xB0`)是二进制帧,没有 JSON Schema(见 §3)。

## 2. 文件清单

| 文件 | 消息 | TYPE | 方向 | 契约节 |
|---|---|---|---|---|
| `common.schema.json` | 共享 `definitions`(非消息) | — | — | §3.5、§3.6、§4、§6、§7.1 |
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

共 **36 个消息 schema + 1 个公共定义文件**(`common.schema.json` 含 **31 个 `definitions`**,含 v1.1 的 `modResolved`)。契约 §3 登记 37 个消息类型,减去**二进制帧** `DATA_CHUNK`(见 §2.1 / 本文 §3)后覆盖率 **36/36 = 100%**,无遗漏、无多余;每个 schema 的 `title` 里的 TYPE 与契约逐一核对一致。

`fixtures/` 是**真实载荷样例**(HELLO / HELLO_ACK 的普通/首次配对/已绑定 / CAPS 三槽位 / PING / PONG / ERROR / TX_REQUEST 两阶段),由 `tools/validate_schemas.py` 在 `jsonschema` 可用时逐条做 Draft7 求值——**这是"CAPS 样例必须过 schema"(docs/06 TC-008)的可执行载体**。

## 3. 二进制布局:`DATA_CHUNK`(`0xB0`)为什么没有 JSON Schema

契约 §2.1 规定**数据帧(`TYPE` 最高位 = 1)的 `PAYLOAD` 不是 JSON**,而是 §3.5 的二进制块(RLE 跳变序列)+ 尾部 JSON 元数据。因此不存在 `msg-data_chunk.schema.json`——用 JSON Schema 描述二进制 RLE 布局是错误建模。注意事项:

- **二进制尾部**:布局为 `FLAGS(1B)` + `edge[0]` 起点(`u32 LE` 相对微秒)+ RLE 对(`u16 LE dt_us` + `u8 level`)+ `0x00 0x00` 分隔 + UTF-8 JSON 元数据。字段顺序与字节序**以契约 §3.5 为准**,不要凭 schema 反推;
- **尾部元数据**可用 [`common.schema.json`](./common.schema.json) 的 `definitions.dataChunkMeta`(`f_hz` / `mod` / `rssi_dbm` / `sample_clock_hz` / `long_gaps`)校验;
- **`FLAGS` 两位是二进制头部,不在 JSON 里**:bit0 = OOK/ASK 电平序列 vs FSK 过零/相位序列;bit1 = 判决点(0 = 未经判决的模拟前端序列,保留时序;1 = 芯片已判决位流,时序已丢失)。bit1 的语义被 `features.raw_semantics` 镜像(契约 §3.6 规则 4),发射方向固定置 0(契约 §3.7);
- **`dt_us` 饱和值 `0xFFFF`** 表示超长间隔,真实值按顺序落在 `long_gaps[]` 里——解析器不得把饱和值当真实间隔;
- **布局的字节级黄金向量(byte-level fixture)尚未入库**,是待补项(TODO)。在补上之前,只能以契约 §3.5 的文字 + 固件 `frame.c` 为准;**不要**宣称该布局已被 schema 或测试向量覆盖。

## 4. 字段变更规则

> **任何字段改动,先改 [`docs/04-protocol.md`](../../docs/04-protocol.md)(必要时升版本),然后在同一批改动里同步本目录,再通知固件与 App 两侧。**

合并前自检清单:

1. [ ] `sha256sum docs/04-protocol.md` 与本文顶部记录值一致;(`tools/validate_schemas.py` 会自动比对,不一致即 FAIL)
2. [ ] 契约新增/改名/删除的字段已同步到对应 `msg-*.schema.json`(字段名逐字符一致);
3. [ ] 新增枚举值已同步进 `common.schema.json` 的 `enum`(错误码/WARN 码见**契约** §6,状态见契约 §3.2/§3.7,调制见契约 §3.4/§3.7,配对失败见契约 §7.1(1.1));
4. [ ] 契约新增消息类型 → 新增 `msg-<类型小写下划线>.schema.json`,并更新本文 §2 表格;
5. [ ] 契约删除字段 → 按**契约** §8 的流程先在 `CAPS.deprecated` 标记再删;schema 侧同步;
6. [ ] `features` 的 v1.1 字段仍**不**进 `required`(理由:契约 §3.6 规则 5"缺失 = 未知"、§7.4);若契约改为"必须提供",才移入 `required`;
7. [ ] 更新本文顶部的契约 sha256;
8. [ ] `python3 tools/validate_schemas.py` 退出码 0(含 fixtures 求值;若本机没有 `jsonschema` 则只跑结构检查并如实提示);
9. [ ] `for f in protocol/schemas/*.json; do python3 -m json.tool "$f" >/dev/null || echo BAD $f; done` 无输出(注意:跨文件 `$ref` 的**求值**不能这样跑,见 §5)。

**版本口径**:`VER`(`0x01`)只在**帧格式**变化时升(契约 §8),新增消息类型/新增字段**不升版本**。因此 schema 不单独维护语义版本号,由"镜像契约的 sha256 + git 历史"体现;设备与 App 必须**按字段而不是按版本号**适配。

## 5. 校验命令

```bash
# 1) 仓库自带结构自检(纯标准库,任何环境都能跑):JSON 合法性、顶层键、draft-07、
#    $id 与文件名一致且唯一、msg-* 声明 type、带非空 properties 的对象必须显式声明
#    additionalProperties、required ⊆ 有效 properties(含 $ref/allOf/oneOf 分支)、
#    enum 非空无重复、每个 $ref 的本地文件存在且 #/definitions/... 可解析。
#    若本机装了 jsonschema,还会自动追加:37 个 schema 的 Draft7 check_schema、
#    fixtures/ 正例求值、内置负例必须被拒绝、契约 sha256 比对。
python3 tools/validate_schemas.py            # 退出码 0 = 全部通过

# 2) 单文件 JSON 语法检查(不涉及跨文件 $ref)
for f in protocol/schemas/*.json; do python3 -m json.tool "$f" > /dev/null || echo "BAD: $f"; done

# 3) 完整 schema 求值(需要 jsonschema;本仓库环境已装 4.x)
#    ⚠️ 本目录的 $id 是裸文件名(离线优先),CLI 没有 base URI 时会把
#       "common.schema.json" 当成无法抓取的 URL ⇒ 20 个含跨文件 $ref 的消息必失败。
#       必须给 --base-uri,且要在 protocol/schemas/ 下就地执行(不需要网络):
cd protocol/schemas
python3 -m jsonschema --base-uri "file://$PWD/" -i fixtures/caps.json msg-caps.schema.json
python3 -m jsonschema --base-uri "file://$PWD/" -i fixtures/tx_request_stage2.json msg-tx_request.schema.json
#    负例(必须 exit 1):
python3 -m jsonschema --base-uri "file://$PWD/" \
    -i <(printf '%s' '{"armed_ack":true,"reason":"x"}') msg-tx_request.schema.json

# 4) 另一种可选工具(本机没有 check-jsonschema;装上后同样需要能解析相对 $ref):
#    check-jsonschema --schemafile protocol/schemas/msg-hello.schema.json payload.json
```

当前状态:37 个 schema 文件(36 消息 + 1 公共)、31 个 `definitions`、73 处 `$ref`(全部本地相对路径)、11 个 fixtures;`python3 tools/validate_schemas.py` 退出码 0。

> **校验范围(如实说明)**:`tools/validate_schemas.py` 的**结构检查始终执行**;完整 schema 求值**按 `jsonschema` 是否可用自动启用**,未安装时只做结构检查并明确打印"已跳过"。历史文档曾写"本机没有 pip、没有 jsonschema"——那是**事实错误**(环境有 `jsonschema 4.x`、`pip`),已改为机会式校验。求值能覆盖 `type`/`enum`/`required`/`additionalProperties`/`oneOf`/`allOf` 的语义,但**不能**表达契约里"由运行时状态决定"的约束(见 §7.5)。

## 6. 编写约定

### 6.1 严格双轨(有意的,不要"修好"它)

| 轨道 | 规则 | 理由 |
|---|---|---|
| **开发期(本目录)** | 每个消息/嵌套对象都设 `additionalProperties: false` 并列出 `required` | 抓拼写错误与字段漂移,让契约改动在 schema diff 里显形 |
| **运行时(固件/App 解析器)** | **必须宽松忽略未知字段**;未知消息类型回 `ERROR{unsupported}` | 契约 §1「前向兼容」、§8「新增字段不升版本」 |

也就是说:**schema 说"未知字段不合法" ≠ 解析器可以拒绝未知字段。** 去掉 schema 的 `additionalProperties: false` 会失去拼写错误检测;让运行时拒绝未知字段,会在固件/App 版本错配时直接崩溃。

### 6.2 `$id`、`$ref` 与 `definitions`

- 顶层 `$id` 用**文件名本身**(如 `"msg-hello.schema.json"`),不用 `https://...` 绝对 URL——本仓库离线可用,绝对 `$id` 会把相对 `$ref` 解析成需要联网抓取的 URL;
- 共享定义放在 `common.schema.json` 的 **`definitions`**(draft-07 的关键字;2019-09 才叫 `$defs`,本目录**不得**使用 `$defs`,校验脚本会报错);
- 跨文件引用一律相对路径:`{"$ref": "common.schema.json#/definitions/rssi"}`;`common.schema.json` 内部自引用用 `{"$ref": "#/definitions/band"}`;
- 字段说明与 `$ref` 并存时用 `allOf` 包裹(draft-07 中 `$ref` 的同级关键字会被忽略);
- **不要**把 `additionalProperties: false` 的对象放进 `allOf`/`oneOf` 的**分支**里——每个分支会把对方的属性视为"额外属性"而互相拒绝。因此:
  - `SCAN_HIT`(带 `scan_id`)与 `SCAN_HITS.hits` 的元素(`definitions.hitPoint`,不带 `scan_id`)虽然同形也分开定义;
  - `TX_REQUEST` 的两阶段用 `oneOf` 表达:第一阶段分支只写 `required`,第二阶段分支**只**声明 `armed_ack` 并允许 `additionalProperties:false` 来禁止夹带参数;顶层统一 `additionalProperties:false`(见 §6.3)。
- 用 CLI 求值时跨文件 `$ref` 需要 `--base-uri`(见 §5 第 3 组命令)。

### 6.3 类型与 `required`

- 频率一律 `integer` Hz;RSSI/功率用 `number`(dBm,可带小数,分 `rssi` 与 `powerDbm` 两个定义);时间 `*_ms` 用 `number`;计数量用 `integer`;
- `*_confidence` 为 `["number","null"]`(0.0–1.0 或 null);猜测量用 `guessString`/`guessInt`(可 null)——契约 §3.6 规则 1/2;
- 空载荷消息(契约写 `{}`)为 `type: object` + `additionalProperties: false` 且无 `properties`;
- `payload_rle` 为 `string`(base64url,契约 §3.7);
- `features.mod` 用 `modResolved`(ook/2fsk,**不含 auto**);`auto` 只属于接收侧请求语义(CAPTURE_START 的 `mod`);
- `required` 判定:契约列出的字段一律进 `required`;**例外**是契约自身写明条件性的字段:
  - `TX_REQUEST.auth_token` 仅在 `tx_requires_token` 为真时必需;
  - `TX_REQUEST.armed_ack` 只在**第二阶段**出现,第一阶段不得出现——所以两阶段的 `required` 由 `oneOf` 的两个分支分别声明,顶层不再写 `required`;
  - `TX_STATUS.granted_power_dbm` 在 `armed`/`sent`/`failed` 时必须给出;
  - `features` 的 v1.1 字段(理由见 §4 第 6 条 / §7.4)。

## 7. 与契约的已知口径差异与推断登记

本目录有 21 处 description 引用"判断依据";以下逐条登记,**只报告,不擅自改契约**。若契约日后明确其中一项,请同步把对应 schema 的推断改成契约原文并更新本表。

| # | 字段 / 文件 | 现状 | 判断与依据 | 复核 |
|---|---|---|---|---|
| 7.1 | `MODULES` vs `CAPS.modules` | 两者元素字段不一致 | 契约 §3.2 为 `{slot, chip, present, bands, tx_max_dbm, driver_version, spi, ant}`,§4 为 `{slot, chip, present, bands, tx_max_dbm, tx_allowed}`;schema 分别定义 `definitions.moduleInfo` 与 `definitions.capsModule`,**两边都不擅自补齐** | 契约评审 |
| 7.2 | `HELLO_ACK.last_accepted_seq` | 契约 §7.1(1.2) 为"设备**应**回传"(SHOULD) | schema 登记为**可选**,口径一致但强度弱于"必须";若要强校验,移入 `required` 即可 | 契约评审 |
| 7.3 | `STATUS` / `SAVED_LIST` 元素结构 | 契约 §9.1 明确未定义 | 两者建模为**开放对象**(元素不设 `additionalProperties:false`),否则会拒绝一切真实载荷 | 契约 §9.1 |
| 7.4 | `features` 的置信度字段 | 契约 §3.6 要求估计量/猜测量必带同名置信度 | schema 的 `required` 只含 v1 的 14 个字段,置信度字段可选(依 §3.6 示例 + 规则 5"缺失 = 未知");"设备必须真的产出置信度"由测试把关,不靠 schema 必填 | 契约 §3.6、§4-6 |
| 7.5 | schema 表达不了的约束 | 由实现与测试兜底 | `hits_dropped = hits_total − hits_pushed`;`truncated = true ⇒ features.incomplete = true`;`max_edges` 钳制并发 `WARN{edge_clamped}`;`tx_whitelist` 为空 ⇒ 禁止一切发射;`auth_token` 的参数绑定与 `seq` 严格递增;`ble_frame_max` 随承载变化 | 契约 §2.4/§3.3/§3.4/§7.1 |
| 7.6 | `SCAN_ID`(`scanId`) | 类型与起始值未规定 | 按**非负整数**建模;设备分配、缓存保留到下次扫描(契约 §3.3) | 契约 §3.3 |
| 7.7 | 接收侧 `mod` vs 发射侧 `mod` | §3.7 载荷行只写裸 `mod` | 接收侧沿用 §3.4 的 `mod`(ook/2fsk/auto);发射侧用 `txMod`(不含 auto);`features.mod` 用 `modResolved`(不含 auto) | 契约 §3.4/§3.7 |
| 7.8 | `policy.tx_whitelist` 元素类型 | 契约未写明元素类型 | 契约 §4 示例为空数组;按 §1「频段白名单」与 §6 `band_out_of_range` 推断元素为 `band`(`[f_start_hz, f_stop_hz]`) | 契约 §1/§4/§6 |
| 7.9 | `CAPS.deprecated` 形状 | 契约 §4 示例/limits 表均无形状 | 按 §8 展示的数组语法建模为可选 `string[]`(元素类型属推断) | 契约 §8 |
| 7.10 | `GET_SCAN_HITS.offset` | 契约只写"分页取回" | 未定义 0/1 起始与越界行为,按**从 0 开始**建模 | 契约 §3.3 |
| 7.11 | `GET_SCAN_HITS.limit` | 契约未规定上限 | 不设 `maximum`;设备侧按自身缓冲与 `stream_rate_hz` 决定 | 契约 §3.3 |
| 7.12 | 空对象请求 | `GET_STATUS` / `LIST_SAVED` / `OTA_BEGIN` 的请求载荷未给字段 | 按其它空载荷请求(§3.1 GET_CAPS、§3.2 LIST_MODULES)的写法建模为 `{}` 等价空对象 | 契约 §3.8/§9 |
| 7.13 | `MODULES` / `SAVED_LIST` 顶层为 array | 与"顶层为 object"的通用约定不符 | 契约 §3.2 明写 MODULES 载荷是数组;SAVED_LIST 同理属列表,按契约建模为 array | 契约 §3.2/§3.8 |
| 7.14 | `PONG.t_ms` 语义 | 契约 §3.1 未明确是回显 PING 值还是设备本地时刻 | `PING`/`PONG` 都按 number 建模,不约束语义;§9.1 已登记 v1.2 明确 | 契约 §3.1/§9.1 |
| 7.15 | `SAVE_SIGNAL.raw_ref` 类型 | 契约未规定 | 按"引用标识"建模为 `string`(不约束是否文件名/ID) | 契约 §3.7 |
| 7.16 | `SCAN_PROGRESS.pct` 上下界 | 契约未给 | 按百分比语义取 `0..100`;若将来改为可超 100(多轮)须同步 | 契约 §3.3 |
| 7.17 | `TX_STATUS.code` / `detail` | 契约未明确取值空间 | `code` 疑似取自 §6 错误码(如 `tx_denied`/`tx_bad_token`/`arm_timeout`)但未写明,故 `code`/`detail` 均按自由文本建模,不设 enum | 契约 §3.7/§6 |
