# App 与 AI 分析

> 本文只记 Android PWA 客户端(载体、连接配对、UI、本地数据)与 App 侧 DeepSeek 链路。
> 产品范围见 [docs/01-product.md](01-product.md);设备行为见 [docs/03-firmware.md](03-firmware.md);协议字段以 [docs/04-protocol.md](04-protocol.md) 为唯一权威;验收项见 [docs/06-test-plan.md](06-test-plan.md)。
> ⏸ BLE 与 Wi-Fi 通道已移出本批:通道择优、状态保持、无线配对与配网流程一律不实现,仅保留能力定义供后续批次。

## 1. 载体与结论

**仅 Android,主载体 PWA(HTML/JS + Service Worker);传输层 = 一层抽象 + 两个后端,基线为 WebUSB**。页面必须是 `https://`(secure context),`requestDevice()` 必须由用户手势触发。

| 项 | 结论 |
|---|---|
| 平台范围 | 仅 Android;iOS 不支持,不做双端原生 |
| 主载体 | PWA:免安装、免审核、服务端发版即生效;本机零新工具链(Node v22.22.1 + npm 已装) |
| 传输基线(后端 B) | `navigator.usb` + `web-serial-polyfill` —— **必须能工作**的那条 |
| 传输优化(后端 A) | `navigator.serial`,探测到才用;**不得作为硬依赖** |
| 设备过滤 | 控制口 = 标 `COM` 的 Type-C(板载 CH343P;现场确认丝印为 `COM`,不是 `OTG`),过滤器写死 `vendorId 0x1a86 / productId 0x55d3`;**禁止** `classCode:2` 通配;原生 USB 口(`0x303a`)在本板从未枚举成功,不可用。外壳必须标出控制口 |
| 门禁 | 真机 `claimInterface` + 字节回环仍须验证(TC-074a,见 [docs/06-test-plan.md](06-test-plan.md));失败则只把 USB 读写包成原生 Android USB Host 薄壳,UI 与业务逻辑仍留 Web 层 |

**为什么是 WebUSB 而不是 Web Serial**:Android 的 Web Serial 到 Chrome 148(2026-04)才引入,以它为基线等于把最低系统要求钉死在 148+;WebUSB 自 Chrome 61 级长期可用。规范层面无障碍:WebUSB 的受保护接口类只有 Audio/HID/MassStorage/Hub/SmartCard/Video/A-V/Wireless,CDC(`0x02`)与 CDC-Data(`0x0A`)都不在其中。Chrome 官方文档另给出决定性事实:Android 默认**没有** CDC-ACM 驱动("一个值得注意的遗漏"),串口类设备通常未被系统占用,因此 Android 反而比桌面更可用;而桌面 Linux/macOS 的 `cdc_acm` 常先占用接口。Google 的 polyfill 实现的就是 Web Serial API(该规范的原型实现),两个后端同形,上层只写一套逻辑。

**桌面自测口径**:Windows 上 USB 串口由系统驱动占用且 WebUSB 还需 WinUSB ⇒ **大概率不可用**(Web Serial 可用)。不得用 Windows 浏览器自测 WebUSB,否则会把环境问题误判为固件缺陷;桌面自测走 Linux/macOS,或显式走 Web Serial 路径。这不影响 Android 的产品路径。

## 2. 连接与配对

USB 是本批**唯一**通道:控制 + 配对 + 桌面调试都要经过它,它一断即无任何指挥手段。首次连接目标 ≤ 30s(A-01)。

| 步骤 | 内容 |
|---|---|
| 1 | 用户用 OTG 数据线连接手机与设备(设备自行供电,不依赖手机 OTG 供电);线必须是数据线,只有电源两芯的充电线插上不通信 |
| 2 | App 经 WebUSB/polyfill 打开 CDC-ACM 口(921600,`requestDevice` 由用户手势触发;`claimInterface` 失败必须原样报错,不得吞掉)。⚠️ CH343P 的 DTR/RTS 接自动复位:**打开端口会让设备重启一次**,App 必须容忍,等启动完成后再握手,不得当成连接失败 |
| 3 | `HELLO{app_version, proto_min, proto_max, client_id, pairing:true}`,**不带 `pair_code`**(字段保留,本批不校验) |
| 4 | 设备在 USB 链路上回 `HELLO_ACK{pair_ok:true, first_bond:true, pairing_secret:"<32B base64url>"}` |
| 5 | `GET_CAPS` 并与 `proto` 求交集;无交集则提示升级并终止 |
| 6 | `seq := max(本地值, HELLO_ACK.last_accepted_seq)`(防 App 重装后被永久拒绝发射),然后写设备档案(`device_id` + `pairing_secret` + `last_accepted_seq`)进 IndexedDB |

**首次配对无 `pair_code`**:USB 是物理端口,插线这一行为本身就是"在场证明",结构上不需要一次性码。配对失败的活路径只有两条:`already_bound`(提示在**设备端按键**确认后重配,插线不能替代按键)、`rng_failed`(安全模块异常,拒绝配对,不得降级为弱随机)。

| 重连情形 | 处置(顺序固定) |
|---|---|
| 心跳超时 | `PING{t_ms}`/`PONG{t_ms}` 间隔 5s,连续 3 次(15s)判断线;RTT 兼作链路质量指示。App 被系统挂起时 `transferIn` 不执行,心跳仍是必要判活。重连退避 1s → 2s → 4s → 8s → 15s 封顶,无限重试但每 5 次向用户报一次 |
| 拔线 | 监听 `navigator.usb` 的 `disconnect` 事件,立即转"通道丢失"UI,**不等心跳超时**;正在进行的捕获作废必须明说 |
| 授权失效 | ① 复用既有 `USBDevice` → ② `getDevices()` 找回 → ③ 重新 `requestDevice()`(一次点击)→ ④ 最后才 `location.reload()` |

⚠️ **授权交互不可静默**:Android 上 `requestDevice()` 后会弹一次系统框,已授权设备再次 `open()` 时**还会再弹一次**。UI 必须预先说明会弹两次,且**不得承诺"自动重连、无需交互"**。整页重载会丢失 WebUSB 授权,只能作最后手段。

**取消无人值守带来的四条硬约束**:① 所有功能必须前台可用,不得依赖后台运行;② 禁止"后台攒数据、回前台拉取",不新增设备侧记录/回读;③ 连接消失 ⇒ 必须立即停止发射,且 App 必须让用户看到这条因果;④ 断连提示必须区分"**线掉了/授权失效**"与"**设备故障或离线**",混为一谈会让用户去解决不存在的问题。

扫频进行中可申请 Screen Wake Lock(`navigator.wakeLock.request('screen')`)避免息屏断连,任务结束或切后台时主动释放。请求规模**必须**读 `CAPS.limits.usb_frame_max` 与 `max_edges`,不得硬编码;`frame_too_large` 时降低请求规模后重发,不得重试同尺寸。

## 3. UI 与交互

| 页面 | 主要元素 | 关键交互 |
|---|---|---|
| ① 设备连接 | 设备卡片(展示名、`device_id`、RTT、配对状态)、添加/忘记设备 | 「用 USB 线配对」为唯一入口;按配对错误码分支引导 |
| ② 模块管理 | 模块列表(`slot`/`chip`/`present`/`bands`/`tx_max_dbm`/`tx_allowed`/`driver_version`/`spi`/`ant`)、活动模块高亮 | 点选 → `SELECT_MODULE`;切模块前警告"会中止当前任务"。`spi.bus` 相同属正常,只有 `cs` 冲突才是配置错误 |
| ③ 扫频 | 起止频率(受 `CAPS.bands` 夹取)、步进、驻留、`max_edges`、阈值滑杆、频谱条 + 瀑布图、命中列表、进度、真实覆盖区间 | 开始/停止;点命中项进入捕获;`WARN{band_clipped}` 把未扫区间画灰;`hits_dropped>0` 必须提示"本次明细不完整"并给"取回全部命中"(`GET_SCAN_HITS` 分页) |
| ④ 捕获与波形 | 波形(RLE 展开)、`edges`/`duration_ms`/`truncated` 徽章、特征面板(含 `*_confidence`、`raw_semantics`、`incomplete`)、解码结论 | `truncated=true` 提示已达 `max_edges`;`incomplete=true` 必须显眼标注"数据不完整" |
| ⑤ AI 分析 | 分析卡片(`signal_type`/`protocol_guess`/`confidence`/`threat_level`、`matched_protocols`、`recommended_actions`、`caveats`)、"已发送内容"折叠区 | 点"分析"上传特征;可展开查看真实请求体;置信度低标黄 |
| ⑥ 信号库 | 已保存信号(名称、频点、调制、来源)、特征预览 | `SAVE_SIGNAL` / `LIST_SAVED` / `DELETE_SAVED`;可批量分析(串行、间隔 ≥1s) |
| ⑦ 发射 | 目标参数、四道门禁结果、二次确认区、`TX_STATUS` 结果 | 见下方发射确认流程;`tx_whitelist` 为空或无 `pairing_secret` ⇒ 整页只读 |
| ⑧ 设置与审计 | DeepSeek Key 管理、同意书、发射审计镜像、数据清理 | 导出审计(JSON/CSV)、切换模型、一键清除本地数据 |

**扫频可视化**:三种视图并存且共享同一份命中数据(不各自维护状态)——频谱条(横轴频率/纵轴 RSSI,阈值画水平参考线)、瀑布图(横轴频率/纵轴时间/颜色映射 RSSI,用于分辨持续载波与周期突发)、命中列表(`f_hz`、`rssi_dbm`、`t_ms`、**与噪声底之差**)。三条诚实性要求:显式区分"已扫区间"与"请求但未覆盖区间",不得把未扫画成"无信号";`hits_dropped>0` 必须提示明细不完整;命中一律按 `scan_id` 归属,换扫描后旧明细不可再取。

**功率展示与调节**(实际生效功率由设备回报,App 不自行计算):

| 模块 | `tx_max_dbm`(硬件) | 出厂默认(满功率) | 能否到 20dBm |
|---|---|---|---|
| M1 AS10(SI4463)/ M3 AS01(nRF24L01P+PA) | 20 | 20dBm | ✅ |
| M2 AS07(CC1101) | **10** | 10dBm | ❌ **硬件上限,不是策略限制** |

设备级 `policy.tx_power_cap_dbm` = 20,有效上限 = `min(请求值, 20, 该模块 tx_max_dbm)`;显示以 `TX_STATUS.granted_power_dbm` 为准,本地计算只作即时反馈。步进 ≤ 1dB,显示"请求值 → 生效值";滑杆止于 `tx_max_dbm` 而非 20。用户对 M2 要求 20dBm 时,文案必须是「实际 10dBm —— 当前模块(AS07)硬件上限为 10dBm,与功率设置无关;换用槽位 1/3 可达 20dBm」,**禁止**写成"被设备限制/被策略拒绝"。降低功率无需额外确认,提高功率必须在二次确认弹窗中显式显示生效值。

**发射确认流程**:四道门禁 ① `policy.tx_whitelist` 含该 `f_hz`(空 = 禁止一切发射)② `power_dbm ≤ tx_power_cap_dbm` 且 `≤ module.tx_max_dbm` ③ 人工二次确认(勾选"已获授权" + 长按 3s,弹窗展示频点/调制/请求值→生效值/重复次数/触发它的 AI 结论与 `caveats`)④ 授权令牌 `HMAC-SHA256(pairing_secret, device_id|seq_be32|f_hz_be64|mod|power_dbm_x10_be16)`,截断 128bit 后 base64url。前三道任一不过即在本地拒绝、不发帧;令牌侧要求 `seq` 每设备单调递增并持久化,**无时间窗**(设备无 RTC)。`armed → sent` 两段式按协议实现,`armed` 期间射频零输出。AI 结论只能变成建议文本,**任何自动发射路径都不允许存在**;审计镜像必须同时记 `power_dbm`(请求)与 `granted_power_dbm`(生效)。合规门禁整体已降级为参考设计(屏蔽室场景),但四道门禁结构保留为纵深防御与防误操作,拒绝与审计逻辑仍须验收(A-05)。

## 4. 数据模型与存储

| 数据 | 存储 | 保留与清理 |
|---|---|---|
| 设备档案、设置、同意书 | IndexedDB(**不用 localStorage**:同步 API、容量小) | 直到"忘记设备";忘记时一并删 `pairing_secret`,此后需设备端按键重新配对 |
| `pairing_secret`、DeepSeek API Key | IndexedDB + WebCrypto 非可导出密钥包装 | 目标是"不落明文";纯 Web 环境做不到真正的密钥保护 |
| 信号库(特征 + 可选原始码流) | IndexedDB,原始码流用 Blob 分表(1000 跳变 ≈ 3KB) | 原始码流 100 条 / 30 天;可设"不保存原始码流"只留特征 |
| AI 分析历史 | IndexedDB,唯一索引 `feature_hash` | 500 条 / 90 天,先到者先删、索引同删 |
| 发射审计镜像 | IndexedDB,**append-only**,与设备侧环形日志互为副本 | **不自动删除**,仅用户显式导出后清理,清理前二次确认 |
| 波形/瀑布渲染中间态 | 仅内存 | 可重建,不持久化 |

提供"一键清除全部本地数据";UI 必须说明**设备侧环形日志不受本地清理影响**。

## 5. AI 分析链路

**放在 App 侧的理由**:设备不持有 API Key(设备会被放进车内/背包,存在被物理接触的风险);TLS 与 JSON 处理会与捕获缓冲争内存;换模型只需改一个配置项,设备侧换模型要刷固件;上传边界的告知与开关集中在一处可被验证。推论:`features` 必须**自包含**,设备不参与追问,需要更多信息只能由 App 发起第二次捕获 —— 因此不做多轮对话。

| 参数 | 取值 |
|---|---|
| base_url | `https://api.deepseek.com`(OpenAI 兼容),`Authorization: Bearer <API Key>` |
| 模型(**可配置,不得硬编码**) | **`deepseek-flash`**(DeepSeek-V4.1-Flash);升级复核用 `deepseek-v4-pro`;`deepseek-chat`/`deepseek-reasoner` 已退役、禁止使用 |
| 上下文 / 最大输出 | 1M / 384K token |
| 思考模式 | **显式关闭** `{"thinking":{"type":"disabled"}}`(低 `temperature` 仅在非思考模式下生效) |
| 输出 | `response_format={'type':'json_object'}`,`max_tokens=1200` |

关闭思考模式的理由:这是结构化分类而非推理题,判据已在提示词里给全;思考模式下 `temperature`/`presence_penalty`/`frequency_penalty` 完全失效;思考链按输出 token 计费,成本约 2.9 倍。仅当返回 `unknown` 或 `confidence < 0.4` 时,才用 `deepseek-v4-pro` + 思考模式做**一次**升级复核。

**调用链**:`CAPTURE_DONE{features}` → 规范化特征并稳定序列化 → `feature_hash = sha256` → 查本地分析历史(命中即止)→ 组装提示词 → 校验/重试/降级 → 分析卡片。设备只回传协议 §3.6 定义的 `features`,不发起任何网络请求,不存任何凭据。

**CORS 实测事实**:对 `/chat/completions` 发带 `Origin` 的 `OPTIONS` 预检返回 `access-control-allow-origin` 回显该 Origin、`allow-methods: POST`、`allow-headers: authorization,content-type`;不带 Key 的 `POST` 返回 401 且同样带 `access-control-allow-origin`。⇒ 浏览器/PWA **可直接调用**,不必自建代理;但 Key 暴露在前端的事实不变(只允许用户自带 Key,不内置厂商共享 Key)。这是实测的服务端行为、非官方承诺,应视为可能变化的外部依赖,App 内保留"改走自建代理"的开关。

## 6. 提示词与输出契约

**系统提示词要点**(直接决定结论强度):① 只依据给定字段推断,不得引入未观测事实;② 缺失字段即未知,不猜;③ 尊重 `*_confidence`,< 0.5 必须标为弱证据,`null` 表示设备拒绝猜测、不得替它补值;④ **先解读 `raw_semantics` 再解读时序字段**(`undecided` 为载波周期级、`decided` 为已判决位流、`unknown`/缺失则不做时序推断并写入 `caveats`);⑤ 设备无精确功率测量能力,不得给绝对功率、距离、坐标或"确定是干扰器";⑥ `incomplete=true` 必须下调 `confidence` 且 `caveats` 首条写明"数据不完整";⑦ 不得把"看起来像"写成"就是";⑧ 证据不足用 `unknown`,宁可 unknown 不要编造;⑨ 只输出一个 JSON 对象,不要代码块标记与解释文字。**提示词中必须出现 "json" 字样并给出完整字段示例**——这是 JSON 输出模式的硬性触发条件,不是措辞技巧。

**用户提示词模板要点**:`【实测特征】`块由 `CAPTURE_DONE.features` **原样序列化**填充,App 不得补算额外特征;信噪比、`feature_hash` 由 App 算好后放进"补充说明"(避免模型算错,且派生量可追溯);提示"字段缺失、为 null 或无 `*_confidence` 时在 `caveats` 说明"。为配合服务端前缀缓存,系统提示词必须是 messages 第一条且**字节级稳定**,可变的 `features` 放在最后。

**JSON 输出模式的条件**:① `response_format={'type':'json_object'}`;② 提示词含 "json" 字样并给出期望格式示例;③ `max_tokens` 足以避免 JSON 被中途截断。另有官方承认的已知缺陷:**使用 JSON 输出时可能偶尔返回空 content** —— 空内容必须当作一类**正常失败分支**处理(校验 → 纠错重试 → 降级),不是崩溃。

| 环节 | 规则 |
|---|---|
| 校验 | 本地 JSON Schema 校验:可 `JSON.parse`、顶层为 object、必需字段齐全、类型正确、`confidence ∈ [0,1]`、`threat_level` 在枚举内、三个数组字段为数组;`caveats` 为空数组仅记警告不判失败 |
| 纠错重试 | 仅 1 次:追加模型原始输出 + 具体校验错误,要求只重新输出合法 json;通过则标注"已重试 1 次" |
| 降级 | 仍失败 → 纯文本展示,原始输出原样放入"原始响应"折叠区;`threat_level` 徽章显示"未知",**绝不用默认值兜底**;写一条不含 Key 的本地诊断日志 |
| 超时与重试 | 连接/首字节 10s(`AbortController`);整体 45s(非思考)/ 120s(升级复核);网络错误/429/500/503 指数退避最多 2 次(1s、3s);401/402/400/422 不重试;总调用上限 = 首次 + 纠错 1 次 + 升级复核 1 次 = **最多 3 次** |

**成本口径**(按官方定价):1 个中文字符 ≈ 0.6 token、1 个英文字符 ≈ 0.3 token;单次约输入 1.1K、输出 0.26K token。`deepseek-flash` 谷时:输入(未命中)$0.15/M、输出 $0.60/M、输入(命中)$0.003/M ⇒ 单次 ≈ **$0.00032(≈ ¥0.0023)**,缓存命中 ≈ $0.00019(约 59%);1000 次 ≈ $0.32;误开思考模式约 2.9 倍。每次调用把 `usage`(含 `prompt_cache_hit_tokens`/`prompt_cache_miss_tokens`)记入本地历史以便核对。

**缓存与去重**:`feature_hash`(全字段)命中即复用、不发请求;`feature_hash_stable`(排除随环境波动的 `rssi_dbm`/`noise_floor_dbm`)命中可复用但标注"基于历史相似样本"并给"重新分析"按钮。`*_confidence` 与 `incomplete` 保留在哈希内。服务端前缀缓存为 best-effort,不保证命中。

**离线降级**:L0 有网 + 有 Key 走 DeepSeek;L1 无网/无 Key/用户关闭/调用失败时用同一份判据表在 App 内本地执行,输出同结构结果并标 `source:"local_rules"`,UI 必须写明"本地规则判断,未使用 AI";L2 连本地规则也无法判定时只展示特征与波形,`threat_level = unknown`。**不得用缓存里的旧结论冒充新分析**,必须有来源与时间戳。

## 7. 威胁分级判据

同一份判据用于三处(系统提示词、L1 本地规则库、评测期望标签),不允许各写一套。

| 等级 | 判据(需同时满足) | 建议动作 |
|---|---|---|
| `high` | ① `payload_bits ≤ 32`(且 `payload_bits_confidence ≥ 0.5`)且 `frame_repeats ≥ 3` 且 `repetition_ms < 100`;且 ② `rssi_dbm - noise_floor_dbm ≥ 20`;**或** ③ `edge_count` 高、`duration_ms` 长且频率落在非通用 ISM 频段 | 保留证据 + 定点观测(`SET_RSSI_STREAM`);若为自有设备考虑换编码;**不建议发射** |
| `medium` | 固定码特征存在但重发稀疏(`frame_repeats < 3` 或 `repetition_ms ≥ 100`);或 `33 ≤ payload_bits ≤ 128`;或命中已知协议谱系但无法确认型号 | 多次捕获比对,确认是否同一源 |
| `low` | 命中已知消费类协议指纹(固定 `payload_bits`、`symbol_rate_bps` 落在典型档位、`pulse_width_us.hist` 呈两支分布),观测次数少(疑似单次人工触发) | 归档到信号库,无需特别处置 |
| `info` | `repetition_ms` 稳定(多次观测相对偏差 ≤ 5%,置信度 ≥ 0.5)、`payload_bits` 固定、低频(≤ 1 次/分钟)、`f_hz` 在通用 ISM 频段 | 标记"已知周期源",可加入忽略列表以减少重复计费 |
| `unknown` | `rssi_dbm - noise_floor_dbm < 10`;或 `edge_count < 8`;或关键判据字段为 `null`/缺失;或 `incomplete === true` 且其余证据薄弱;或无法判定调制 | 调整扫频阈值或延长捕获后重试 |

两条通用前提:带 `*_confidence` 的字段置信度 < 0.5 时**不得单独作为判据依据**;`raw_semantics` 必须先行解读,`undecided` 下优先依赖测量量(`edge_count`/`duration_ms`/`f_hz`/`rssi_dbm`)而非估计量。

三条必须写进 `caveats` 的边界:① 设备不做功率标定,判据只出现"与本底之差",绝不出现绝对功率、距离或坐标;② **`high` 不等于"恶意"**(可能是自家车库门),等级只表示值得注意的程度;③ 协议指纹覆盖率有限,库外信号必须落 `unknown`,不得外推到具体协议名。目标使用地未定时,频段判断一律按通用 ISM 处理。

## 8. 隐私与合规告知

| 允许上传 | 禁止上传 |
|---|---|
| 协议 §3.6 的**全部 `features` 字段**(频率、调制、`raw_semantics`、RSSI、边沿与脉宽统计、编码/载荷/CRC 猜测及其 `*_confidence`、重复周期、帧重复数、噪声底、`incomplete`) | ❌ 原始码流(`DATA_CHUNK` / `payload_rle` / RLE 跳变序列)——它是**可重放的信号本体** |
| App 计算的派生量(信噪比等) | ❌ 位置:GPS、Wi-Fi SSID/BSSID、IP 地址 |
| 用户主动填写的备注(可选,默认空) | ❌ 设备标识:`device_id`、`device_name`(可被用于关联) |
| | ❌ 时间戳原值(最多给粗粒度时段,避免行为画像) |

**用户同意**:首次使用 AI 分析前展示一次性同意页,列明将发送的**字段清单**、接收方为第三方(DeepSeek),并提供**"预览实际将发送的 JSON"**按钮(真实请求体,不是截图或摘要)。同意状态与同意文本版本号一起存本地,**文本变更需重新征求同意**;设置页可随时撤回,撤回后立即退回 L1 本地规则库,历史记录保留并标注来源。选择自带 Key 时额外提示"Key 仅存本机,服务端为 DeepSeek"。

**UI 告知**:分析卡片角标写「由 DeepSeek 生成 · 已发送约 1.1K token 的特征 · **不含原始码流**」(写出"不发什么"比只写"已加密"有用);"已发送内容"折叠区展示该次真实 `features` JSON,让告知可被验证;L1 本地结果必须反向标注"本地规则判断,未使用 AI";断网时不出现任何"AI 分析中"的假进度。

**Key 边界**:只允许用户自带,仅存本机、不上传、不参与同步;纯 Web 环境没有安全密钥存储(同源脚本与扩展都能读到),因此**不得内置厂商共享 Key**——若将来需要,必须改为"App → 自建网关 → DeepSeek"的服务端代理。缓解手段叠加:严格 CSP、依赖锁定与完整性校验、`type=password` 不回显不写日志、提供"不启用 AI"模式。
