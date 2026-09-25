# DeepSeek 智能分析链路

状态: draft · 最后更新:2026-09-20 · 上游依据:

- `README.md`(AI 分析固定在 App 侧调用 DeepSeek,设备只回传特征)
- `docs/README.md`(文档索引、待决问题编号表)
- `docs/00-overview/010-product-definition.md`(F-09、F-10、场景 S3、成功判据 A-04)
- `docs/00-overview/040-verified-platform-facts.md`(**已核实平台事实 F-1 / F-1a / F-1b**,DeepSeek 侧事实的唯一记录地)
- `docs/adr/0002-ai-analysis-in-app.md`(`accepted`:AI 分析放 App 侧;§4.3 明确不得自动发射、§4.4 上传边界、§4.5 离线降级)
- `docs/30-protocol/010-device-app-protocol.md`(`v1` + v1.1 修订,权威来源;主要依据 **§3.5** 二进制判决点、**§3.6** 特征与不确定性、§3.7 发射、§4 CAPS、§5 状态机、§6 错误码、§7 安全)
- `protocol/schemas/`(`v1.1`,36 个消息 schema + `common.schema.json`;`features` 字段清单的机器可读来源)
- `docs/40-app/010-mobile-app-design.md`(载体选型、通道管理、本地数据模型)

> 本文件是**设计文档**,不含任何实现代码。
> DeepSeek 平台事实**不在本文重复核实**,一律引用 `040-verified-platform-facts.md` F-1 系列;未核实项写「待确认」。

---

## 1. 为什么把 AI 分析放在 App 侧

**决策依据:[ADR-0002](../adr/0002-ai-analysis-in-app.md)(`accepted`)。** 该 ADR 已完整比较「App 侧 / 设备直连 / 可切换」三方案,此处只摘与本链路直接相关的四条:

| 理由 | 说明 |
|---|---|
| **设备不持有 API Key** | 契约 §1、§7 与 ADR-0002 §2 三条一致。设备会被放在车内/背包,存在被物理接触的风险;Key 在里面等于泄露面直接归零的收益丧失 |
| **省内存与算力** | 主控 ESP32-S3(16MB Flash / 8MB PSRAM,约束 C-01)。TLS 握手与 JSON 处理会与 RMT 捕获缓冲、Wi-Fi/BLE 协议栈争内存(ADR-0002 §2「内存预算要留给射频」) |
| **可换模型** | 模型迭代很快 —— 本次核实就发现模型名已变更、`deepseek-chat`/`deepseek-reasoner` 退役(F-1)。App 侧换模型是改一个配置项;**设备侧换模型要刷固件** |
| **合规与审计点集中** | 「哪些特征被上传给第三方」的答案集中在 App 一处,可在一个页面里展示与开关(ADR-0002 §4.4、§4.5)。若分散到设备,无法向用户保证边界 |

ADR-0002 §4.1 还给出一个对本链路影响最大的推论:**设备不参与 AI 交互,无法「按需追问」,所以 `features` 必须自包含**。AI 若需要更多信息(更长捕获、另一频点对照),只能由 App **发起第二次捕获** —— 这直接决定了提示词不能设计成多轮问答(§9 非目标)。

### 1.1 本链路的模型与参数选型(事实来源:F-1 / F-1a)

| 项 | 取值 |
|---|---|
| base_url(OpenAI 兼容) | `https://api.deepseek.com` |
| 鉴权 | `Authorization: Bearer <API Key>` |
| **模型**(可配置,**不得硬编码**) | **`deepseek-flash`**(DeepSeek-V4.1-Flash);升级复核用 `deepseek-v4-pro` |
| ⚠️ **已退役,禁止使用** | **`deepseek-chat`、`deepseek-reasoner`**。旧名 `deepseek-v4-flash`、`deepseek-v4-flash-vision-exp` 仍被接受但模型已退役,由 V4.1-Flash 承接并按 Flash 价格计费(F-1) |
| 上下文 / 最大输出 | 1M / 384K token(F-1a) |
| JSON 输出 | ✅ 支持,但有确切触发条件(F-1b,见 §4.1) |
| Tool Calls | ✅ 支持;另有 `strict` 模式(Beta,见 §4.3) |
| 思考模式 | **默认开启**,默认 effort `high`;开关 `{"thinking":{"type":"enabled/disabled"}}`([Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)) |
| 上下文缓存 | 磁盘缓存**默认开启**,无需改代码;命中需与某个 cache prefix unit **完整匹配**([Context Caching](https://api-docs.deepseek.com/guides/kv_cache)) |

**本链路选型:`deepseek-flash` + 显式 `{"thinking":{"type":"disabled"}}` + 非思考模式。** 三条理由:

1. 这是**结构化分类**任务而非推理题,判据已在提示词中给全(§6);
2. 思考模式下 `temperature` / `presence_penalty` / `frequency_penalty` **完全失效**(官方原文:不报错但也不生效),而分类任务需要低温度的确定性([Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode));
3. 思考链作为**输出 token 计费**,直接抬高成本(§5.1 的量化对比)。

仅当返回 `unknown` 或 `confidence < 0.4` 时,才用 `deepseek-v4-pro` + 思考模式做**一次升级复核**。

---

## 2. 调用链

```
┌──────────────┐   ① CAPTURE_DONE{features}     ┌───────────────────────────────┐
│  设备(ESP32) │ ─────────────────────────────► │      App(手机,AI 调用方)      │
│              │  仅契约 §3.6 特征,无原始码流    │                               │
│ · 采集跳变    │                                │ ② 规范化 features             │
│ · 归纳特征    │                                │    → 稳定序列化(canonical)    │
│ · 标注不确定度│                                │    → feature_hash = sha256    │
│ · 不做网络    │                                │ ③ 查本地分析历史(命中即止)   │
└──────────────┘                                │ ④ 组装提示词(§3)             │
        ▲                                       │ ⑤ 校验 + 重试 + 降级(§4)     │
        │ ⑧ (可选) TX_REQUEST / SAVE_SIGNAL     │ ⑥ 展示分析卡片                │
        │    必须经人工二次确认 + 令牌(§9)      └───────────────┬───────────────┘
        │                                                       │ HTTPS POST
        │                                                       ▼
        │                                       ┌───────────────────────────────┐
        │                                       │  api.deepseek.com             │
        │                                       │  /chat/completions            │
        │                                       │  model: deepseek-flash        │
        │                                       │  response_format=json_object  │
        │                                       │  thinking: disabled           │
        │                                       └───────────────┬───────────────┘
        │                                                       │ 严格 JSON
        │                                                       ▼
        └─────────────────── ⑦ App 展示 + 可选下发动作 ◄─────────┘
                             (动作永远需要人工确认)
```

### 2.1 浏览器能否直接调 DeepSeek —— 已实测的 CORS 事实

**实测(2026-09-20)**:对 `https://api.deepseek.com/chat/completions` 发 `OPTIONS` 预检(带 `Origin: https://example.com`),返回:

```
HTTP/2 200
access-control-allow-origin: https://example.com     ← 回显请求 Origin
access-control-allow-methods: POST
access-control-allow-headers: authorization,content-type
access-control-allow-credentials: true
```

同域下不带 Key 的 `POST` 返回 `401 Authentication Fails`,并**同样带 `access-control-allow-origin`**。

**结论**:从浏览器/PWA **直接调用 DeepSeek 在 CORS 层面可行**,不需要自建代理。这解除了「PWA 必须先搭网关」的顾虑,但**把 Key 暴露在前端的风险原样保留**(App 文档 §8.3):纯 Web 环境没有安全密钥存储,Key 只应允许**用户自带**,不应内置厂商共享 Key。

> 口径说明:这是**实测的服务端行为**,不是 DeepSeek 的书面承诺 —— 官方文档未为浏览器直连做任何保证。上线前应把「CORS 回显」当作**可能变化的外部依赖**,并在 App 内保留「改走自建代理」的开关。若该事实变化,应登记进 `040-verified-platform-facts.md` 而非只在本文件改。

---

## 3. 提示词契约

### 3.0 前提:提示词必须消费契约 §3.6 的不确定性表达

契约 v1.1 的 `features` 有**三级不确定性**,这是**防止 AI 幻觉的第一道闸门**(契约 §3.6 原文即如此表述):

| 类别 | 字段 | 表达方式 |
|---|---|---|
| **测量量** | `f_hz` `rssi_dbm` `edge_count` `duration_ms` `pulse_width_us` `noise_floor_dbm` `frame_repeats` | 直接给值 |
| **估计量** | `symbol_rate_bps` `repetition_ms` | 必带同名 `*_confidence`(0.0–1.0) |
| **猜测量** | `encoding_guess` `payload_bits` `preamble_bits` `crc_guess` | 必带同名 `*_confidence`;**证据不足时值为 `null`(= 设备拒绝猜测)** |

另有三个**语义开关**,提示词必须显式处理:

- `raw_semantics`:`"undecided"`(模拟前端未判决序列)/ `"decided"`(芯片已判决位流)/ `"unknown"`(**设备无法判断**)。**它决定 `symbol_rate_bps` 与 `pulse_width_us` 该怎么解读** —— 同是 FSK,过零序列的跳变间隔是载波周期级,已判决位流是符号率级,**量纲完全不同**(契约 §3.5)。把它忽略会让模型把「过零点间隔」当成「符号宽度」从而误判协议。
- `incomplete:true` → 特征基于被截断或丢帧的数据算出。**AI 必须降低结论强度**,App 必须显式标注「数据不完整」(契约 §3.6 规则 3)。
- 缺失的 `*_confidence` **不得当作 1.0**;缺失表示「设备未提供」,按「未知/低」处理(契约 §3.6 规则 1)。

因此 §3.1 的系统提示词把这三件事都写成了硬性规则。

### 3.1 系统提示词(完整草稿,可直接复制)

```text
你是射频信号分析助手,服务于一个便携式多频段射频工具。我会以严格 JSON 形式给你一组**由设备实测得到的信号特征**(频率、调制方式、原始序列语义、RSSI、噪声底、边沿统计、脉冲宽度分布、编码猜测、重复周期、载荷位数、前导位数、CRC 猜测、帧重复次数,以及各项的置信度)。

你必须遵守以下硬性规则:

1. **只依据我给出的字段推断。** 不得引入任何未在输入中出现的观测事实(不得假设设备位置、不得假设信号来源的物理地点、不得假设设备型号、不得假设周围环境)。
2. **输入里没有的字段,就是未知。** 若某字段缺失或为 null,不要猜测其值;必要时在 caveats 里说明「因缺少 X 字段,该结论为弱推断」。
3. **置信度必须被尊重。** 任何形如 *_confidence 的字段,若其值低于 0.5,你在使用该字段时必须明确标注它是弱证据;若某字段为 null,其含义是「设备判定证据不足,拒绝猜测」——你不得替设备补一个值,也不得因为它是 null 就当作「无此特征」。
4. **必须先解读 raw_semantics 再解读时序字段。**
   - 若 raw_semantics = "undecided":输入是未经判决的模拟前端数字化序列,脉冲宽度与跳变间隔反映的是**载波周期级**信息,pulse_width_us 与 symbol_rate_bps 不能直接当作符号宽度/符号率;
   - 若 raw_semantics = "decided":输入是芯片内部已判决的位流,时序信息已丢失,不应从 dt 反推频偏;
   - 若 raw_semantics = "unknown" **或该字段缺失**:说明**设备无法判断 / 未提供**载荷语义。此时你**不得**假定它是 `undecided`(那等于替设备谎报时序量纲),应**避免从时序字段反推符号率与频偏**,并把「载荷语义未知」写进 caveats。区分:字段缺失可能是固件较旧,`"unknown"` 是设备明确表示无法判断——两者对结论的处理相同(都不做时序推断),但后者说明设备支持 v1.1。
5. **设备不具备精确功率测量能力**,只有 RSSI 与噪声底估计。因此**不得**给出绝对功率、距离、发射源坐标或「确定是干扰器」这类结论。
6. **若 incomplete 为 true**,说明数据来自被截断或丢帧的捕获:你必须下调 confidence,并在 caveats 的第一条写明「数据不完整」。
7. **不得把「看起来像某协议」写成「就是某协议」。** 结论强度必须由 confidence 体现,并在 caveats 中说明不确定性来源。
8. 若证据不足以判断,`signal_type` 用 "unknown"、`threat_level` 用 "unknown",并在 caveats 说明缺什么。**宁可 unknown,不要编造。**
9. 你的输出**必须是一个 JSON 对象**(json),不要输出 Markdown 代码块标记、不要输出任何解释性前后缀文字、不要输出思维过程。只输出一个 json 对象。

威胁分级判据(必须按此判断,不得自创标准;凡引用到带 *_confidence 的字段,该置信度必须 ≥ 0.5 才能作为判据依据):

- "high":载荷极短(payload_bits ≤ 32 且 payload_bits_confidence ≥ 0.5)且重复密集(frame_repeats ≥ 3 且 repetition_ms < 100)且信号显著高于本底(rssi_dbm - noise_floor_dbm ≥ 20);或持续/宽频能量占空比异常(edge_count 高、duration_ms 长)且频率落在非通用 ISM 频段。
- "medium":固定码特征存在但重复稀疏;或载荷位数中等(33–128);或命中已知协议谱系但无法确认具体型号。
- "low":命中已知消费类协议指纹(如常见 OOK 遥控/门磁编码),发射次数少、疑似单次人工触发。
- "info":周期高度稳定(repetition_ms 相对偏差 ≤ 5% 且 repetition_ms 的置信度 ≥ 0.5)、payload_bits 固定、低频次(≤ 1 次/分钟)、频率在通用 ISM 频段 —— 典型如气象传感器/门磁/无线表计的周期上报。
- "unknown":信噪比不足(rssi_dbm - noise_floor_dbm < 10),或 edge_count < 8,或 incomplete 为 true 且其余证据本身薄弱,或关键字段缺失/null,或无法判定调制方式。

输出 JSON 的字段与取值范围(所有字段都必须出现):

{
  "signal_type": "字符串,信号类型的中文描述,如「固定码遥控发射」「周期性温湿度传感器上报」「未知窄带载波」",
  "protocol_guess": "字符串,最可能的协议名或「未知」;可带型号如「EV1527 类似固定码」",
  "confidence": 0.0,
  "modulation": "字符串,如 OOK/ASK、2FSK、未知",
  "encoding": "字符串,如 PWM、Manchester、未知",
  "threat_level": "info 或 low 或 medium 或 high 或 unknown",
  "threat_reason": "字符串,用输入字段说明为何给出该等级,必须引用具体字段值",
  "matched_protocols": ["字符串数组,可能匹配的协议,可为空数组"],
  "recommended_actions": ["字符串数组,建议的下一步动作,只允许建议类动作,不得建议自动发射"],
  "caveats": ["字符串数组,不确定性与缺失字段的说明,必须非空"]
}

再次强调:只输出 json,不要输出其他任何内容。
```

> 设计要点:
> - 规则 9 与结尾重复出现 **"json"** 字样,并给出完整字段示例 —— 这是官方 JSON 输出模式的**硬性触发条件**(F-1b)。
> - 规则 3/4/6 是 v1.1 契约新增能力的**消费端**;若提示词不写这三条,设备辛苦标注的 `*_confidence` / `raw_semantics` / `incomplete` 会被模型整个忽略,§3.0 那道闸门就白设了。
> - `recommended_actions` 显式禁止「建议自动发射」,与 §9 非目标、ADR-0002 §4.3 呼应。

### 3.2 用户提示词模板(完整草稿)

```text
请分析下面这一组设备实测特征,按 json 格式输出结论。

【实测特征】
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

【补充说明】
- 上述 features 由设备固件在捕获结束时归纳得出,是**唯一**可依据的信号载荷。
- 计算得到的信噪比(rssi_dbm - noise_floor_dbm)为 32.5 dB。
- 若某字段缺失、为 null 或未提供 *_confidence,请在 caveats 中说明。

请只输出一个 json 对象。
```

> App 实现细节:`【实测特征】` 块必须由**设备回传的 `CAPTURE_DONE.features` 原样序列化**填充,**不要**由 App 计算或补全额外特征 —— 否则「不得编造未观测到的事实」这条约束会在提示词层面自相矛盾。信噪比、`feature_hash` 这类派生量**建议由 App 显式算出并作为「补充说明」给出**,而不是让模型自己减:既避免模型算错,又让派生量可追溯。
>
> **字段缺失时的处理**:契约 v1.1 的 `features.required` 仍只含 v1 的 14 个字段,新增的 `*_confidence` / `raw_semantics` / `incomplete` 是**可选**(`protocol/schemas/README.md` §4.4 与 §7 第 14 项)。因此提示词不能假设新字段一定存在 —— §3.1 规则 3/4/5 已覆盖「null / 缺失」两种情形。

---

## 4. 结构化输出实现

### 4.1 用 JSON 输出模式保证可解析

官方要求**四条同时满足**(F-1b;[JSON Output](https://api-docs.deepseek.com/guides/json_mode)):

| # | 要求 | 本设计如何落实 |
|---|---|---|
| 1 | `response_format` 设为 `{'type': 'json_object'}` | 每次请求固定带该参数 |
| 2 | **提示词(system 或 user)中必须包含 "json" 字样**,并给出期望 JSON 的示例 | §3.1 系统提示词中多次出现 "json",并给出完整字段示例 |
| 3 | 合理设置 `max_tokens`,避免 JSON 被中途截断 | 设 `max_tokens = 1200`(非思考模式输出约 250–400 token,留 3× 余量) |
| 4 | **官方明确的已知缺陷**:使用 JSON 输出时「API may occasionally return empty content」 | 把「空内容」当成**一类正常的失败分支**处理,见 §4.2 |

`deepseek-flash` 上下文 1M / 最大输出 384K,`max_tokens=1200` 远低于上限,不会触发服务端截断。

### 4.2 校验失败 → 纠错重试 1 次 → 降级

```
请求(第 1 次)
   │
   ├─ HTTP 非 2xx ──────────────► §4.4 错误分支
   ├─ 200 但 content 为空 ──────► 视为校验失败(F-1b 官方已承认的已知行为)
   └─ 200 且非空
        │
        ▼
   ① JSON.parse 失败?
   ② 顶层不是 object?
   ③ 缺少必需字段 / 类型不符 / confidence ∉ [0,1] / threat_level ∉ 枚举?
        │
        ├── 全部通过 ──────────► 展示 + 写入历史
        └── 任一失败
              │
              ▼
        纠错重试(第 2 次,仅 1 次)
        在原 messages 后追加:
          {"role":"assistant","content":"<模型上次的原始输出>"}
          {"role":"user","content":"上面的 json 不合法。问题:<校验器给出的具体错误,如
            'threat_level' 的值 'HIGH' 不在允许集合内>。请只重新输出一个合法的 json
            对象,字段与取值范围必须与系统提示一致。不要输出任何解释。"}
              │
              ├── 通过 ──────────► 展示(并标注「已重试 1 次」)
              └── 仍失败
                    │
                    ▼
            降级:纯文本展示
            · 把模型原始输出原样放进「原始响应」折叠区(不做二次加工)
            · 主卡片显示「结构化解析失败,以下为模型原始回答」
            · threat_level 徽章显示「未知」,**绝不用默认值兜底**(避免假结论)
            · 写一条本地诊断日志(不含 Key),便于后续改进提示词
```

**校验器设计要点**:用一份**本地 JSON Schema**(字段与 §3.1 一一对应,并参考 `protocol/schemas/common.schema.json` 的 `$defs.confidence` 等定义保持一致)做校验,而不是手写 if 链;`matched_protocols` / `recommended_actions` / `caveats` 必须是数组;`caveats` 为空数组记一条警告但**不判失败**(避免因格式细节丢掉一次可用结果)。

**额外一条 v1.1 相关校验(建议加)**:若输入 `features.incomplete === true` 而 `caveats` 中没有任何一条提到「不完整」,记一条**质量警告**并写入诊断日志 —— 这是 §8 评测中要统计的一类失败模式。

### 4.3 更严格的替代方案(记录备选,本阶段不启用)

DeepSeek 提供 Function Calling 的 **`strict` 模式**(Beta):`base_url` 换成 `https://api.deepseek.com/beta`,每个 `function` 设 `"strict": true`,由服务端按 JSON Schema 校验;支持类型 `object`/`string`/`number`/`integer`/`boolean`/`array`/`enum`/`anyOf`,且**每个 object 的所有属性都必须列入 `required`、`additionalProperties` 必须为 `false`**([Tool Calls](https://api-docs.deepseek.com/guides/tool_calls))。

**本阶段不启用的理由**:① `/beta` 端点的稳定性承诺更弱;② 它要求把提示词约束翻译成受支持的 Schema 子集,而本设计的判据需要 `minLength`/`maxItems` 这类**官方明确列为 unsupported** 的约束;③ 引入 `tools` 后,思考模式下 `reasoning_content` 必须逐轮回传,一旦漏传会 **400**([Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)),复杂度明显上升。**待 schema 稳定、且需要更强保证时再评估。**

### 4.4 超时与重试上限

| 项 | 值 | 依据 |
|---|---|---|
| 连接/首字节超时 | 10 s | App 强制 `AbortController` |
| 整体超时 | 45 s(非思考)/ 120 s(升级复核) | 思考模式更慢 |
| 网络错误 / 429 / 500 / 503 | 指数退避重试 **最多 2 次**(1s、3s) | 官方对 429/500/503 的建议都是「稍后重试」([Error Codes](https://api-docs.deepseek.com/quick_start/error_codes)) |
| 401 / 402 / 400 / 422 | **不重试**,直接报错 | 重试无意义;401 →「Key 无效」、402 →「余额不足」(同上) |
| 结构化校验失败 / 空内容 | 纠错重试 **1 次** | §4.2 |
| 总调用上限 | **首次 + 纠错 1 次 + 升级复核 1 次 = 最多 3 次** | 防止失控计费 |

每次调用都把服务端返回的 `usage`(含 `prompt_cache_hit_tokens` / `prompt_cache_miss_tokens`)记进本地历史,用于 §5 的成本核对。

---

## 5. 成本与批量策略

### 5.1 一次分析的 token 估算(给出算式)

换算口径来自官方([Token & Token Usage](https://api-docs.deepseek.com/quick_start/token_usage)):**1 个中文字符 ≈ 0.6 token,1 个英文字符 ≈ 0.3 token**。单价来自 F-1a / [Models & Pricing](https://api-docs.deepseek.com/quick_start/pricing)。

> ⚠️ **口径差异说明**:`040-verified-platform-facts.md` F-1a 给出的量级是「输入约 0.7–1.2K token、输出约 0.3–0.6K token」。本节按 v1.1 扩充后的提示词(含不确定性规则)与扩充后的 `features` 重算,落在该区间的上半段:**输入 ≈ 1.1K、输出 ≈ 0.26K**。

| 组成 | 估算量 | 换算 | token |
|---|---|---|---|
| 系统提示词(§3.1) | ≈ 1150 汉字 + 约 350 ASCII(JSON 示例) | 1150×0.6 + 350×0.3 = 690 + 105 | **≈ 795** |
| 用户提示词模板文字(§3.2) | ≈ 150 汉字 | 150×0.6 | ≈ 90 |
| `features` JSON(v1.1,21 字段) | ≈ 700 ASCII 字符 | 700×0.3 | ≈ 210 |
| **输入小计** | | | **≈ 1095** |
| 输出 JSON(非思考) | ≈ 300 汉字 + 约 250 ASCII | 300×0.6 + 250×0.3 = 180 + 75 | **≈ 255** |
| 输出 JSON(若误开思考模式) | 上述 + 约 800–1500 reasoning token | — | **≈ 1050–1750** |

**单次成本(`deepseek-flash` + 非思考 + **谷时** + 缓存未命中)**:

```
输入: 1095 token = 0.001095 M × $0.15/M = $0.000164
输出:  255 token = 0.000255 M × $0.60/M = $0.000153
合计 ≈ $0.00032 / 次        (按 1 USD ≈ 7.2 CNY 折算 ≈ ¥0.0023 / 次)
```

**同条件但缓存命中**(系统提示词 + 模板构成稳定前缀时):

```
前缀(795+90=885)命中: 0.000885 M × $0.003/M = $0.0000027
features 未命中:       0.000210 M × $0.15/M  = $0.0000315
输出:                                        = $0.000153
合计 ≈ $0.00019 / 次        (约为未命中的 59%)
```

**若误开思考模式**(reasoning ≈ 1000 token):输出变 1255 token → `0.001255 × $0.60 = $0.000753`,单次 ≈ **$0.00093**,是基准的 **约 2.9 倍**。这就是 §1.1 显式关闭思考模式的量化理由。

**规模感受**:1000 次分析 ≈ **$0.32**(非思考/谷时/未命中);若全部改用 `deepseek-v4-pro` + 思考模式,则约 **$3.9**,相差一个数量级。

### 5.2 缓存与去重:避免重复分析同一信号

ADR-0002 §4.2 已要求「对 `features` 做规范化哈希,命中缓存则直接复用分析结果」。**三道闸门,按代价从低到高:**

1. **`feature_hash` 去重(App 侧,零成本,最优先)**
   - 规范化:对 `features` 做**稳定序列化**(对象 key 按字典序、数字统一为固定小数位且不带指数记法),再 `sha256`。
   - **必须排除在哈希之外的字段**:`rssi_dbm`、`noise_floor_dbm`(随环境波动)。建议维护两份哈希:
     - `feature_hash` —— 全字段(判「完全同一份数据」);
     - `feature_hash_stable` —— 排除环境量(判「同一设备、同一频点、同一协议」的跨次重复)。
   - 命中 `feature_hash` → **直接复用历史结论**,不发请求;命中 `feature_hash_stable` → 允许复用但标注「基于历史相似样本(N 分钟前)」并提供「重新分析」按钮。
   - ⚠️ **v1.1 注意**:`*_confidence` 与 `incomplete` 会随捕获条件轻微变化。建议把它们**保留在哈希内**(它们影响结论,不能算作噪声);若发现同一信号因置信度抖动而反复 miss,再考虑对置信度做分桶(如 0.1 粒度)后入哈希。
2. **前缀缓存(服务端,自动)**:官方磁盘缓存默认开启,命中要求**完整匹配某个 cache prefix unit**(F-1a 指出命中价仅为未命中的 1/50,值得设计)。两条硬约束:
   - **系统提示词必须放在 messages 的第一条且保持字节级稳定** —— 任何改动(多一个空格、一次版本号插值)都会让前缀失配;
   - 用户提示词里**可变的 `features` 必须放在最后**,固定模板部分放在其前面,使「system + 模板」这段前缀可复用(官方 Example 2 描述的正是这种「system + 固定内容」被持久化为前缀单元的情形)。
   - 缓存是 best-effort,不保证 100% 命中;应记录 `prompt_cache_hit_tokens` 核对真实命中率。
3. **批量分析(用户显式发起)**:信号库页提供「批量分析选中项」。批量时**串行 + 间隔 ≥1s**,避免撞 429(官方建议「放慢请求节奏」)。默认**不自动批量** —— 每次上传都是用户可见、可取消的动作。

### 5.3 离线 / 无网 / 未配置 Key 时的降级

ADR-0002 §4.5 已要求「无网络时 App 必须能降级」。**三级降级,App 必须始终可用:**

| 级别 | 条件 | 行为 |
|---|---|---|
| L0 完整 | 有网 + 有 Key | 走 DeepSeek,展示 AI 卡片 |
| **L1 本地规则库**(兜底,默认可用) | 无网 / 无 Key / 用户关闭 AI / 调用失败 | 用 §6 判据表**在 App 内本地执行**,输出同样的字段结构,`confidence` 打上 `source: "local_rules"` 标记,`protocol_guess` 用内置轻量指纹表(PT2262/EV1527 类固定码、常见 OOK 遥控、周期传感器的频点+脉冲宽度+重复周期组合)。**UI 必须明确标注「本地规则判断,未使用 AI」** |
| L2 仅特征 | 本地规则也无法判定 | 只展示 `features` 与波形,`threat_level = unknown`,提示「需要联网或配置 Key 才能进一步分析」 |

关键原则:**离线时不得用缓存里的旧结论冒充新分析** —— 必须有明确的时间戳与来源标记。L1 与 L0 的结论在 UI 上**视觉可区分**(徽章 + 文案),否则用户会把规则库判断误当成模型结论。

---

## 6. 威胁分级判据(可执行,不是标签)

判据表同时用于三处:① 写进系统提示词(§3.1);② L1 本地规则库(§5.3);③ 评测集的期望标签定义(§8)。**三处共用一份定义,不允许各写一套。**

| 等级 | 判据(需同时满足) | 依据字段 | 建议动作 |
|---|---|---|---|
| **`high`** | ① `payload_bits ≤ 32`(**`payload_bits_confidence ≥ 0.5`**)且 `frame_repeats ≥ 3` 且 `repetition_ms < 100`(固定码高频重发);**且** ② `rssi_dbm - noise_floor_dbm ≥ 20`(显著高于本底);**或** ③ 能量占空比异常:`edge_count` 高且 `duration_ms` 长且频率落在**非通用 ISM 频段** | `payload_bits`,`payload_bits_confidence`,`frame_repeats`,`repetition_ms`,`rssi_dbm`,`noise_floor_dbm`,`edge_count`,`duration_ms`,`f_hz` | 建议**保留证据 + 定点观测(`SET_RSSI_STREAM`)**;若为自有设备,考虑更换编码。**不建议发射** |
| **`medium`** | 固定码特征存在但**重发稀疏**(`frame_repeats < 3` 或 `repetition_ms ≥ 100`);**或** `33 ≤ payload_bits ≤ 128`;或命中已知协议谱系但无法确认具体型号 | 同上 + `encoding_guess`,`preamble_bits` | 多次捕获比对,确认是否为同一源 |
| **`low`** | 命中已知消费类协议指纹(常见 OOK 遥控/门磁编码形态:固定 `payload_bits`、`symbol_rate_bps` 落在典型档位、`pulse_width_us.hist` 呈两支分布),且观测次数少(疑似单次人工触发) | `symbol_rate_bps`,`pulse_width_us`,`encoding_guess`,`payload_bits` | 归档到信号库;无需特别处置 |
| **`info`** | `repetition_ms` 稳定(多次观测相对偏差 ≤ 5%,且 `repetition_ms` 的置信度 ≥ 0.5)且 `payload_bits` 固定且**低频**(≤ 1 次/分钟)且 `f_hz` 在通用 ISM 频段 —— 典型为气象传感器/门磁/无线表计周期上报 | `repetition_ms`,`payload_bits`,`f_hz` | 标记为「已知周期源」后可**加入忽略列表**,减少重复分析与重复计费 |
| **`unknown`** | `rssi_dbm - noise_floor_dbm < 10`(信噪比不足);**或** `edge_count < 8`;**或** 关键判据字段为 `null`/缺失;**或** `incomplete === true` 且其余证据本身薄弱;**或** 无法判定调制(`mod:"auto"` 且 `encoding_guess` 为 `null`) | `rssi_dbm`,`noise_floor_dbm`,`edge_count`,`mod`,`encoding_guess`,`incomplete` | 调整扫频阈值或延长捕获后重试 |

**判据的两条通用前提(写进提示词与本地规则库)**:

1. **任何带 `*_confidence` 的字段,置信度 < 0.5 时不得单独作为判据依据。** 这是 v1.1 三级不确定性在**判断逻辑**上的落地 —— 否则 `crc_confidence: 0.22` 这种「赤裸的猜测」会照样驱动分级。
2. **`raw_semantics` 必须先行解读**(§3.0)。在 `"undecided"` 下,`symbol_rate_bps` 与 `pulse_width_us` 的量纲不能按符号宽度理解;此时应优先依赖测量量(`edge_count`/`duration_ms`/`f_hz`/`rssi_dbm`)而不依赖估计量。

**必须写明的三条边界(也要进 `caveats`)**:

1. **设备不做功率标定**(产品定义 §3.1 非目标)—— 判据里只出现「与本底之差」,**绝不出现绝对功率、距离或坐标判断**。
2. **`high` 不等于「恶意」。** 一条固定码发射完全可能是自家车库门。等级只表示「值得注意的程度」,`threat_reason` 必须把该点讲清。
3. **协议指纹覆盖率有限。** 产品定义 §3.1 已明确「不承诺解调任意未知协议」。库外信号**必须落 `unknown`**,不允许为了「给个答案」外推到某个具体协议名 —— 这是评测集里要专门测的失败模式(§8.2 的「库外拒答率」)。

---

## 7. 隐私与合规

> 本节以 **ADR-0002 §4.4「上传内容边界」** 为准,并把契约 v1.1 新增字段纳入核对。

### 7.1 上传给第三方的内容边界

| 允许上传 | 禁止上传 |
|---|---|
| 契约 §3.6 的**全部 `features` 字段**:`f_hz`、`mod`、`raw_semantics`、`rssi_dbm`、`edge_count`、`duration_ms`、`symbol_rate_bps` + `*_confidence`、`pulse_width_us`、`encoding_guess` + `*_confidence`、`repetition_ms`、`payload_bits` + `*_confidence`、`preamble_bits`、`crc_guess` + `*_confidence`、`frame_repeats`、`noise_floor_dbm`、`incomplete` | ❌ **原始码流**(`DATA_CHUNK` / `payload_rle` / RLE 跳变序列) |
| App 计算的派生量(信噪比等) | ❌ **位置**:GPS、Wi-Fi SSID/BSSID、IP 地址 |
| 用户主动填写的备注(可选,默认空) | ❌ **设备标识**:`device_id`、`device_name`(无必要,且可被用于关联) |
| | ❌ **时间戳原值**(可上传粗粒度如「夜间/日间」,不上传绝对时间,避免行为画像) |

理由:① 原始码流是**可重放的信号本体**,上传它等于把「能发射什么」交给第三方;② 位置与设备标识与诊断无关,却是最强的关联标识。(ADR-0002 §4.4 允许「用户显式勾选导出」原始码流 —— 那是**用户主动导出**行为,与本处「AI 分析自动上传」是两件事,不得混同。)

### 7.2 是否需要用户显式同意:需要

- **首次**使用 AI 分析前展示一次性同意页,内容包含:将发送的**字段清单**、接收方为第三方(DeepSeek)、以及一个**「预览实际将发送的 JSON」**按钮(不是截图、不是摘要,是真实请求体)。
- 同意状态与同意文本版本号一起存本地;**同意文本变更需重新征求同意**。
- 设置页随时可撤回;撤回后立即退回 L1 本地规则库(§5.3),已产生的历史保留在原处并标注来源。
- 若用户选择「自带 Key」,额外提示「Key 仅存本机,服务端为 DeepSeek」。

### 7.3 如何在 UI 上告知

1. **分析卡片角标**:「由 DeepSeek 生成 · 已发送约 1.1K token 的特征 · 不含原始码流」—— 把「不发什么」写出来,比只写「已加密」有用。
2. **可展开的「已发送内容」折叠区**:展示那次请求的真实 `features` JSON(与 §7.1 边界一致),让告知**可被验证**。
3. **L1 本地结果反向标注**:本地规则结论必须显示「本地规则判断,未使用 AI」,避免两者混同。
4. **不上传即不上传**:断网时 UI 不出现任何「AI 分析中」的假进度。

---

## 8. 可评估性:AI 结论质量的评测集思路

### 8.1 评测集构成

| 项 | 设计 |
|---|---|
| 规模 | **第一阶段 60 条**(可增量扩充);每条 = 一份 `features` JSON + 期望标签 |
| 样本来源 | ① 自有可控发射(在合法白名单频段发射已知编码,采集真机特征)——**优先**,标签可信;② 公开协议文档推得的特征参数(**合成样本,单独分组统计,不与真机样本混算**);③ 真实环境采集后**人工标注**(标注者需给出依据,不止给标签) |
| 每条的期望标签 | `signal_type`(归一化受控词表)、`modulation`、`encoding`、`protocol_guess`(或 `未知`)、`threat_level`、**「是否属于库外协议」标志位** |
| 分组 | 真机可控 / 真机环境 / 合成;**已知协议**与**库外协议**必须分开统计 |
| 附带维度(v1.1 新增,建议纳入) | 每条样本标注 `raw_semantics` 与 `incomplete`,以检验「模型是否尊重不确定性」而不只是「答对没有」 |

### 8.2 指标

| 指标 | 定义 | 门槛(建议) |
|---|---|---|
| **`threat_level` 一致率** | 与人工标注完全相同(5 分类)的比例 | ≥ 85% |
| **高危漏报率(最重要)** | 标注为 `high` 但模型给出 `info`/`low` 的比例 | **必须为 0**(release 阻断项) |
| 高危误报率 | 标注非 `high` 但模型给 `high` 的比例 | ≤ 10% |
| `signal_type` Top-1 准确率 | 受控词表下的准确率 | ≥ 70% |
| `protocol_guess` 命中率 | 仅统计**已知协议**组 | ≥ 70% |
| **库外拒答率(关键)** | 库外协议组中模型输出 `unknown` 或明确「未知」的比例 | ≥ 80% —— 直接检验 §6 边界 3 |
| **不确定性尊重率(v1.1 新增)** | ① 输入 `incomplete=true` 的样本中,`caveats` 至少一条提到「不完整」的比例;② 输入含 `*_confidence < 0.5` 的样本中,该字段**未被当作确定事实**使用的比例 | ≥ 90% |
| 一致性 | 同一样本重复调用 5 次,标签完全一致的比例 | ≥ 90%(非思考 + 低温度下应更高) |
| 稳健性 | 对 `rssi_dbm` 抖动 ±6dB、`edge_count` 随机抽稀 20% 后重跑,`threat_level` **翻转率** | ≤ 10% |
| 结构化成功率 | 一次调用即通过本地 JSON Schema 校验的比例 | ≥ 95%(否则提示词需改) |
| 成本 | 每条平均 input/output token 与实付(用 `usage`) | 与 §5.1 估算偏差 ≤ 50% |

### 8.3 评测方法

1. **离线回放**:评测脚本按 §3 组装提示词逐条调用,记录原始响应、解析结果、`usage`;产物是「样本 × 预测 × 期望」三列表。
2. **三方对照**:同一批样本同时跑 ① DeepSeek(§3 提示词)、② L1 本地规则库(§6 判据表)、③ 人工标注。若**本地规则库在同一批上的 `high` 漏报率更低**,说明 AI 引入的是成本而不是价值 —— 这是一个必须允许得出的结论。
3. **提示词回归**:任何提示词改动后**必须重跑全量评测集**,把分数写进提交说明;`threat_level` 一致率下降 > 5 个百分点即视为回归。
4. **对抗用例**:专门构造「看起来像 high 但实际是自家遥控器」「看起来像 info 但载荷异常」「`incomplete=true` 却有明显结论」「`*_confidence` 极低却被当事实」四类边界样本,分别检验 §6 的三条边界与 §3.0 的闸门。
5. **人工抽检持续化**:从真实使用中抽 20 条/周人工复核,不一致的补进评测集 —— 评测集是**活文档**。

> 与验收判据的关系:A-04 只要求「提交特征后返回结构化分析结果」,是**功能可用性**验证;本节是**质量**验证。两者不要混为一条证据。

---

## 9. 非目标

| 非目标 | 说明 |
|---|---|
| ❌ **不做自动闭环发射** | ADR-0002 §4.3 + 契约 §7.1(4)。AI 结论**只能**变成 `recommended_actions` 里的建议文本(提示词已显式禁止「建议自动发射」)。从结论到 `TX_REQUEST` 之间**必须**插入四道门禁:① `policy.tx_whitelist` ② `tx_power_cap_dbm`/`tx_max_dbm` ③ 人工二次确认(勾选授权 + 长按)④ 授权令牌(HMAC-SHA256 + 单调 `seq`,**无时间窗** —— 设备无 RTC)。**任何「AI 说发射就发射」的路径都不允许存在**,包括「用户开了自动模式」这种说辞 |
| ❌ **不做云端历史存储** | 分析历史只存本地(IndexedDB,见 App 文档 §6),不上传任何服务端;设备侧也不存 AI 结论。清理由用户显式触发 |
| ❌ 不做多轮对话 / 长期会话 | 每次分析是**单轮、无状态**请求;不把历史结论拼进上下文 —— 既省 token,也避免前一次错误结论污染后一次。ADR-0002 §4.1 已说明设备无法「按需追问」,需要更多信息时由 App **发起第二次捕获**后重新分析 |
| ❌ 不上传原始码流 | 见 §7.1(用户显式导出是另一回事) |
| ❌ 不做模型微调 / 自建模型 | 只做提示词工程与评测 |
| ❌ 不做众包指纹库 | 不把用户采集的特征汇聚共享(隐私与合规风险不成比例) |
| ❌ 不用 AI 结论替代设备侧硬约束 | 频段白名单与功率上限**始终**由设备强制(契约 §1「发射默认拒绝」),AI 无论如何输出都不能绕过 |

---

## 10. 对协议与相邻文档的反馈(**只报告,不修改**)

> 契约 v1.1 已关闭了本文件早期草稿中的多项缺口(`features` 字段级契约、`auth_token` 定义、`SCAN_DONE` 计数、ADR-0002、`protocol/schemas/` 非空)。以下仅为**仍未闭环**且影响 AI 链路的项。

| # | 对象 | 问题 | 对本链路的影响 |
|---|---|---|---|
| 1 | 契约 §3.6(部分已在 `protocol/schemas/README.md` §7 第 14 项登记) | `*_confidence` / `raw_semantics` / `incomplete` 在 schema 里是**可选**(为兼容 v1 报文),但契约未写「App/模型在字段缺失时如何降级」的规则 | 提示词已按「缺失 = 未知/低」假设编写(§3.0、§3.1 规则 3/4/5),但这属于**消费端单方面假设**。建议契约补一句,使两端同源 |
| 2 | 契约 §3.6 | ~~`raw_semantics` 只有 `undecided`/`decided` 两值,没有「设备无法判断」的表达~~ — ✅ **已关闭(契约 v1.1)**:已增加第三值 `"unknown"`,并明确**设备不得在无法判断时默认填 `undecided`**;缺失与 `"unknown"` 均按「未知」处理,不做时序推断。本文档 §3.1 规则 4 已同步 |
| 3 | 契约 §3.7 / §3.5(已在 schemas §7 第 3 项登记) | `TX_REQUEST.payload_rle` 的**编码未定义**(§3.5 只定义了设备→App 方向的二进制布局) | AI 的 `recommended_actions` 若要转成发射,需要一个明确的载荷构造方式;当前无法闭环 |
| 4 | 契约 §3.8(已在 schemas §7 第 5 项登记) | `STATUS` 载荷**未定义字段名** | 与本链路无直接关系,但 §7.3「已发送内容」之外的设备侧信息展示会受影响 |
| 5 | 产品定义 §7 | **Q-06(目标使用地)未定**,`docs/60-compliance/` 尚未定稿 | §6 判据中「非通用 ISM 频段」的判断依赖当地频段规划。**在 Q-06 定案前,判据表里的频段判断只能按通用 ISM 处理** |
| 6 | `docs/60-compliance/` | 该目录尚无文档 | §6 的 `high` 判据涉及「异常发射源」,与合规章节的措辞应保持一致(避免技术判据与法规口径互相矛盾) |
