# 已核实的平台事实(独立核实记录)

状态:`verified` · 核实日期:2026-09-20 · 核实人:架构 agent(亲自 fetch 官方来源,非二手转述)

> **为什么单独留一份**:这些事实是产品决策的**承重假设**(决定 iOS 能否用、模型该写什么名、模块能不能扫到)。
> 它们会随平台演进而过期。记录下来是为了让"过期"这件事可被发现,而不是让实现基于记忆里的旧版本。
> **每条都必须能追溯到当日抓取的原文链接。** 二手博客、教程站点的说法一律不作为依据。

---

## F-1 DeepSeek API:模型名已变更 ⚠️ 承重

| 项 | 核实结果 |
|---|---|
| base_url(OpenAI 兼容) | `https://api.deepseek.com` |
| base_url(Anthropic 兼容) | `https://api.deepseek.com/anthropic` |
| **当前模型名** | **`deepseek-flash`**、**`deepseek-v4-pro`** |
| 已退役 | `deepseek-chat`、`deepseek-reasoner` |
| 旧名现状 | `deepseek-v4-flash`、`deepseek-v4-flash-vision-exp` 仍被接受,但对应模型已退役,请求由 **DeepSeek-V4.1-Flash** 承接并按 Flash 价格计费 |
| 思考模式 | 请求体支持 `"thinking": {"type": "enabled"}` 与 `"reasoning_effort": "high"` |
| 流式 | `"stream": true/false` |
| 鉴权 | `Authorization: Bearer <API_KEY>` |

**来源**:
- [DeepSeek API Docs — Your First API Call](https://api-docs.deepseek.com/)(2026-09-20 抓取)
- [Models & Pricing](https://api-docs.deepseek.com/quick_start/pricing)(2026-09-20 抓取)
- [JSON Output 指南](https://api-docs.deepseek.com/guides/json_mode)(2026-09-20 抓取)

### F-1a 上下文与计价(官方数字,2026-09-20)

| 项 | `deepseek-flash` | `deepseek-v4-pro` |
|---|---|---|
| 模型版本 | DeepSeek-V4.1-Flash | DeepSeek-V4-Pro-0813 |
| 上下文长度 | **1M** | **1M** |
| 最大输出 | **384K** | 384K |
| 输入(缓存命中)/1M | 峰 $0.006 / 谷 $0.003 | 峰 $0.044 / 谷 $0.022 |
| 输入(缓存未命中)/1M | 峰 $0.30 / 谷 $0.15 | 峰 $1.32 / 谷 $0.66 |
| 输出/1M | 峰 $1.20 / 谷 $0.60 | 峰 $3.96 / 谷 $1.98 |
| 并发上限 | 2500 | 500 |
| Vision | ✓ | ✗ 不支持 |

**峰谷时段**:峰时 = 周一至周五 **01:00–04:00 与 06:00–10:00 UTC**(不含中国法定节假日);其余时间(含周末与法定节假日全天)为谷时。**谷时价格为峰时的一半。**

> 💡 **对成本设计的直接含义**:本项目单次分析输入约 0.7–1.2K token、输出约 0.3–0.6K token。按 flash 谷时价,单次远低于 $0.001。
> 但**上下文缓存命中价仅为未命中的 1/50**($0.003 vs $0.15),所以**稳定的系统提示词前缀**(固定不变的判据表 + 输出格式说明)能显著降本——这是把"系统提示词写死且结构稳定"从"风格偏好"提升为"成本优化手段"的理由。

### F-1b JSON 输出模式的确切条件 ⚠️ 承重

官方要求**三条同时满足**,否则不保证输出合法 JSON:

1. 设置 `response_format` 为 `{'type': 'json_object'}`
2. **系统或用户提示词中必须包含 "json" 这个词**,并**给出期望 JSON 格式的示例**
3. 合理设置 `max_tokens`,**防止 JSON 字符串被中途截断**

**⚠️ 官方明确的已知缺陷**(原文):*"When using the JSON Output feature, the API may occasionally return empty content. We are actively working on optimizing this issue. You can try modifying the prompt to mitigate such problems."*

→ **空内容不是异常,是官方承认的已知行为。** App 必须把它当成**正常错误路径**处理(校验 → 纠错重试 → 降级),而不是当作不该发生的崩溃。这一条必须写进 App 的重试策略。

**对实现的影响**:
- ❌ 任何文档、提示词、代码里**不得**出现 `deepseek-chat` / `deepseek-reasoner` 作为推荐模型
- ⚠️ 模型名必须**可配置**,不得硬编码——本次核实就已经发现一次改名,未来还会变
- ⚠️ 提示词里**必须出现 "json" 字样并提供输出示例**(这是功能开关,不是提示技巧)
- ⚠️ 必须处理 `content` 为空字符串的情况
- 其他指南页(存在性已从官方侧边栏确认):[Tool Calls](https://api-docs.deepseek.com/guides/tool_calls)、[Thinking Mode](https://api-docs.deepseek.com/guides/thinking_mode)、[Context Caching](https://api-docs.deepseek.com/guides/kv_cache)、[Rate Limit](https://api-docs.deepseek.com/quick_start/rate_limit)、[Error Codes](https://api-docs.deepseek.com/quick_start/error_codes)

---

## F-2 Web Bluetooth 兼容性 ⚠️ 承重

| 平台 | 支持 |
|---|---|
| Chrome(桌面) | ✅ 56+ |
| Edge | ✅ 79+ |
| Opera | ✅ 43+ |
| **Chrome for Android** | ✅ |
| Samsung Internet | ✅ 6.2+ |
| UC Browser for Android | ✅ 15.5+ |
| Baidu Browser | ✅ 13.52+ |
| **Safari(macOS)** | ❌ 全版本不支持 |
| **Safari on iOS / iPadOS** | ❌ **全版本不支持** |
| Firefox(桌面) | ❌ 全版本不支持 |
| Firefox for Android | ❌ |
| Opera Mini / KaiOS | ❌ |

全局支持率:caniuse 记为 **78.02%**。

**来源**:[caniuse — Web Bluetooth](https://caniuse.com/web-bluetooth)(2026-09-20 抓取)

**原文注意事项**:Safari on iOS/iPadOS 无原生支持。存在第三方 Safari 扩展 **iOSWebBLE**(通过桥接 CoreBluetooth 在 JS 层 polyfill `navigator.bluetooth`),但属**用户态支持、非 WebKit 的一部分**,caniuse 的支持状态仍计为不支持。

**对产品的影响**(原为 Q-05 的核心决策依据):

> ✅ **2026-09-20:该约束已被用户决策消解 —— 放弃 iOS,仅支持 Android(见 C-06)。** 因此下面三条不再是设计输入,保留为决策依据的留档:
>
> - **纯 PWA 方案无法在 iPhone 上使用 BLE 近距控制**。场景 S2(近距 BLE 调试)对 iOS 用户不可用
> - 但 **Wi-Fi/WebSocket 通道不受 Web Bluetooth 限制**,iOS 上仍可工作——**前提是解决混合内容限制**(见 F-3)
> - 因此 iOS 用户的可行路径是:仅用 Wi-Fi 通道;或改用原生/包装方案获得 BLE 能力
>
> **Android 侧的结论(仍然有效、且是本期的实际依据)**:Chrome for Android、Edge、Samsung Internet、UC、Baidu 均支持 Web Bluetooth,**BLE 没有平台缺口**,所以 PWA 在 Android 上可以覆盖 F-07 + F-08 + 配网全流程。完整载体选型见 [`../40-app/010-mobile-app-design.md`](../40-app/010-mobile-app-design.md) §1。

---

## F-3 混合内容限制:HTTPS 页面能否连 `ws://` 局域网地址

| 项 | 结论 |
|---|---|
| HTTPS 页面 → `ws://` | ❌ 被浏览器按混合内容拦截(与 `http://` 同级别) |
| HTTPS 页面 → `wss://` | ✅ 允许 |
| HTTP 页面 → `ws://` | ✅ 允许(但 HTTP 页面本身受限,无法安装为可信 PWA、无法用 Web Bluetooth) |

**来源**:
- [W3C Mixed Content(CR 2015-03-17)](https://www.w3.org/TR/2015/CR-mixed-content-20150317/) — 规范正文,「Blockable content is defined as all mixed content that is not upgradable」
- [W3C Bug 13526](https://www.w3.org/Bugs/Public/show_bug.cgi?id=13526) — 规范缺陷记录,原文:*"One distinction we have decided to draw is to prevent downgrading mixed content with websockets (i.e. you cannot connect to a `ws://` url from a `https://` based context)."*
- [MDN — Writing WebSocket client applications](https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_client_applications) — 原文:*"web pages should be served using HTTPS, and the WebSocket connection should use `wss`"*;并指出:构造函数会在不安全连接时抛 `SecurityError`,*"most user agents now require a secure link for all WebSocket connections **unless they're on the same device or possibly on the same network**"*
- [MDN — Mixed content](https://developer.mozilla.org/en-US/docs/Web/Security/Mixed_content) — 分类定义(注:该页当前**未逐字点名 WebSocket**,故此处以 W3C 原文为准)

**⚠️ 重要细微之处(直接影响 iOS 方案可行性)**:MDN 的措辞留了一个口子——**"possibly on the same network"**。即部分浏览器对**同一局域网**内的 `ws://` 目标可能放宽。这意味着行为**因浏览器/版本而异,不能靠推测**。

**对产品的影响**:
- 若 App 是 PWA 且以 HTTPS 部署(为可安装性与 Web Bluetooth,这是必须的),则**可能无法直连设备的 `ws://<局域网IP>:8266`**——具体是否被拦必须**实测**,不可假定
- 可行出路(需在 App 设计文档中定稿):
  1. 设备侧提供 `wss://`(自签证书 + 指纹校验)——**唯一不依赖浏览器宽容度的方案**
  2. App 由本地/受信环境以 HTTP 提供(牺牲 PWA 能力与 Web Bluetooth)
  3. 改用原生 App(不受此限)
- **实现前必须做的实测**(列入验证计划):在 Android Chrome 与桌面 Chrome 上,分别用 HTTPS 页面尝试连局域网 `ws://` 与 `wss://`,记录实际结果

---

## F-4 射频模块频率能力(**模块级,已由厂商官方手册核实**)

> **关键教训(2026-09-20)**:此前本文件只记了**芯片级**能力,导致高估覆盖约 600MHz。**协议与设计只认模块级能力** —— 芯片能覆盖 ≠ 模块能覆盖。

**模块级(权威,直接来自厂商官方手册)**

| 模块 | 芯片 | 模块级可调频率 | 来源 |
|---|---|---|---|
| AS10-M4463D-SMA | SI4463 | **425–525MHz** | [官方资料包](http://www.ashining.com.cn/data/as10_m4463d_sma.zip) `使用手册/as10m4463dsmacn.pdf` §四 |
| AS07-M1101D-SMA | CC1101 | **387–464MHz**(推荐 433±10MHz) | [官方资料包](http://www.ashining.com.cn/data/as07_m1101d_sma.zip) `使用手册/as07m1101dsmacn.pdf` §四 |
| AS01-ML01 | nRF24L01P + RFX2401C | 2400–2525MHz(126 信道) | [官方资料包](http://www.ashining.com.cn/data/as01_ml01.zip) `使用手册/as01ml01cn.pdf` |

**芯片级(仅用于判断"换模块/改前端"的潜力,不可直接当能力用)**

| 芯片 | 芯片级频率能力 | 来源 |
|---|---|---|
| TI CC1101 | **300–348 / 387–464 / 779–928MHz**(三个分立子带) | [TI 产品页](https://www.ti.com/product/CC1101);模块资料包内附 TI 数据手册 |
| Silicon Labs SI4463 | **142–1050MHz**(**全系列,不分 ROM 版本**) | [Si446x 数据手册 Rev 1.4](https://www.silabs.com/documents/public/data-sheets/Si4464-63-61-60.pdf) §Summary |

**⚠️ 一条被证伪的旧结论**:本文件此前写"SI4463 的 C2A 版为 142–1050MHz、B1B 版为 425–525MHz"并据以推断 AS10 搭载 B1B。**这是错的**:
- Si446x 数据手册全系列标 `Si4463 = 142–1050MHz`,**没有任何按版本划分的频率区间**;`Si4463-Bxx-FM3` 的编码规则说明 `Bxx` 是 *ROM 版本 + 固件版本*,与频率无关
- Silicon Labs 规格页写明 **"Si4x6x-C2A 无线电代表了最新一代的 EZRadio"** —— C2A 是**更新**代次,不是窄带老型号
- 真正的约束是**模块前端**:AS10 按 433MHz 设计匹配网络,故只有 425–525MHz;**换芯片版本不会让模块变宽**

**对产品的影响**:见 [ADR-0001](../adr/0001-frequency-coverage-gap.md)。模块级真实覆盖仅 **387–525MHz(约 15%)**,空白 119–300 / 525–779 / 928–1050MHz。

### F-4b 模块制造商与官方文档(2026-09-20 核实)

**制造商确定:三块模块均为「泽耀科技 / Ashining」产品**,官网 `ashining.com.cn`(英文站 `en.ashining.com`)。此前文档里"来源:模块标称"的说法过于含糊——现已定位到厂牌与官方文档入口。

| 模块 | 官方文档标题(原文) | 已核实的官方口径 | 官方文档入口 |
|---|---|---|---|
| AS10-M4463D-SMA | 《AS10-M4463D-SMA 产品手册(Spec_ AS10-M4463D-SMA _V1.1)》 | 基于 **SI4463**、**433MHz**、**100mW**、**SMA** 接口的射频收发模块 | [官网 PDF](http://www.ashining.com.cn/datasheets/as10m4463dsmacn.pdf) · [代理商文档页](https://www.sekorm.com/doc/3565737.html) |
| AS07-M1101D-SMA | 《AS07-M1101D-SMA 产品手册》 | 基于 **CC1101**、**433MHz**、**10mW**、**SMA** 接口 | [代理商文档页](https://www.sekorm.com/doc/3565737.html) · [等价型号规格(E07-M1101D)](https://github.com/TobiasPSP/doneland_sources/blob/main/Components/Data/Data%20Transmission/Wireless/Short%20Range%20Device/FM/FSK/Transceiver/E07-M1101D/README.md) |
| AS01-ML01 | 见下"未核实" | 厂牌确认;**具体型号变体与功率未确认** | [官网产品页](http://www.ashining.com.cn/products_detail.aspx?t=10&cid=254&page=1) |

**AS07-M1101D-SMA 的完整官方规格**(来源为等价型号 E07-M1101D 的规格页,该页明确注明 "also known as AS07-M1101D / AS07-M1101D-SMA"):

| 项 | 值 |
|---|---|
| 频率范围 | **387–464MHz** |
| 发射功率 | **10dBm / 10mW** |
| 接收灵敏度 | **−112dBm** @1.2kbps |
| 调制 | 2-FSK / GFSK / MSK |
| 速率 | 1.2–500kbps |
| 供电 | 1.8–3.6V,典型工作电流 30mA(电源需 >100mA) |
| 接口/尺寸 | SPI,8 脚(GND/VCC/GDO0/CSN/SCK/MOSI/MISO/GDO2),15×28 或 38×10mm |

> ⚠️ **注意区分「芯片支持」与「模块官方口径」。** AS07 模块官方手册的"调制"只列 **2-FSK / GFSK / MSK**,通篇没有 ASK/OOK 字样 ⇒ 按官方口径 **M2 槽位不可用于 OOK 遥控器捕获**。
>
> **但这不是芯片物理限制**:随模块资料包一起取得的 **TI CC1101 数据手册第 94 行明确写支持 "OOK and flexible ASK shaping"**。所以这是**模块级官方口径的缺口**(未列出/未承诺),不是"CC1101 做不到"。若日后需要 AS07 做 OOK,须先向厂商确认固件/寄存器是否放开。
>
> 工程结论不变:需求里的"OOK 解码"(433MHz 遥控器)**必须由 SI4463(M1)承担**,已回写产品定义与硬件文档。

### F-4c ✅ 已解决:官方资料已完整取得(2026-09-20)

**此前 ashining.com.cn 的 HTML 文档路径返回 403 / 交互式 JS 挑战,但 `/data/<module>.zip` 直链可用**(zip 体积 50–59MB,需较长超时或断点续传)。已取得三块模块的完整官方资料包(含使用手册、注意事项、选型手册、Demo 程序、芯片官方数据手册),**以下各项已由官方手册核实**:

| ID | 原未核实内容 | ✅ 结论 |
|---|---|---|
| V-01 | AS10-M4463D-SMA 的可调频率范围 / SI4463 是否 B1B | **工作频段 425–525MHz**(官方手册 §四);且"B1B 才窄带"的推断**被证伪**(见 F-4) |
| V-09 | AS01-ML01 是否真为 20dBm / 含 RFX2401C PA | **确认**。官方手册标题即写"基于 nRF24L01P 带 PA、LNA 和屏蔽盖的 2.4G,**100mW**";产品特征列"采用 RFX2401C 功放芯片,内置 LNA"、"四级功率可调,最大功率约 20dBm"。**风险 R-01 的量级依据成立** |

| 模块 | 资料包 | 关键已核实项 |
|---|---|---|
| AS10-M4463D-SMA | `ashining.com.cn/data/as10_m4463d_sma.zip`(58MB) | 工作频段 **425–525MHz**、20dBm、−126dBm@1kbps、0.123k~1Mbps、30M 晶振、24×16mm |
| AS07-M1101D-SMA | `ashining.com.cn/data/as07_m1101d_sma.zip`(59MB) | 工作频段 **387–464MHz**、10dBm、−111dBm@1.2kbps、0.6k~500kbps、28×15mm、发射电流 29mA |
| AS01-ML01 | `ashining.com.cn/data/as01_ml01.zip`(50MB) | 2.4~2.525GHz 共 126 信道、**最大约 20dBm**、RFX2401C 功放 + 内建 LNA、18×33.3mm、1.8~3.6V、4.9g |

**仍未关闭**:
- AS10 的匹配网络能否重调至更宽频段(需问厂商,**不再阻塞**)

**❌ 已撤销的推断(2026-09-20,用户确认现货即为 `AS01-ML01`)**:
- 此前立过一条"型号后缀需确认",推断"尺寸 18×33.3mm + SMA-K + 2.0km 对应 **`AS01-ML01DP5`**,需求写的裸 `AS01-ML01` 可能是订错" —— **这个推断是错的,已撤销**。
- 错因:官方选型手册的**系列产品对照表只列了加了后缀的衍生型**(`AS01-ML01S`/`ML01D`/`ML01DC`/`ML01IPX`/**DP2**/**DP3**/**DP5**/**DP6**/`SPIPX`/`SP2`),我拿这张衍生表去反推基础型,得出"18×33.3mm 应是 DP5"。而**基础型 `AS01-ML01` 自己就是 18×33.3mm + SMA-K + 20dBm**(手册首页与规格块直接写明,并注明"带 PA、LNA 和**屏蔽盖**")。
- **正确做法**:基础型参数直接查基础型那一节,不要用衍生型号对照表反推。
- **结论**:现货 `AS01-ML01` 与需求描述**逐项一致**,不存在订错后缀的风险,R-01(共存干扰)的量级依据(20dBm)成立。附带确认:**模块带金属屏蔽罩**,这降低了模块内部串扰,但**不改变** 20dBm 对同壳体 Wi-Fi/BLE 接收前端的阻塞风险(屏蔽罩挡不住天线辐射)。

---

## F-4d 开发板身份与引脚事实(2026-09-21 二次核实,⚠️ 承重)

### F-4d-0 🔴 板子身份更正:不是 Waveshare 的那块

**本文件上一版把这块板认成了 Waveshare `ESP32-S3-DEV-KIT-N8R8`,该识别是错的,已撤销。**

| | 上一版(❌ 错) | 本次(✅ 对) |
|---|---|---|
| 板子 | Waveshare ESP32-S3-DEV-KIT-N8R8 | **VCC-GND 源地 `YD-ESP32-S3`** |
| 丝印 | — | **`HW678`**(与用户照片一致) |
| USB 拓扑 | 板载 **CH334 HUB**,两个设备**共用一个** Type-C 口 | **两个物理独立的 Type-C 口**:一个直连原生 USB,一个是 CH343 转串口口 |
| 串口桥 | CH343 | **CH343P**(沁恒 WCH),属实 |

**错因**:上一版**只凭"串口模块是 CH343"这一条**就去检索,命中了同样搭载 CH343 的 Waveshare 板,并把该板的官方原文当成了本板的证据。**"都用了 CH343"不构成"是同一块板"** —— CH343 是通用桥接芯片,被大量开发板采用。这是一次**把搜索结果当证据、而没有先核对物理特征**的错误。

**纠正依据**:用户提供实物照片(丝印 `HW678`、双 Type-C、`IO48, RGB: WS2812`)→ 据此定位到 **VCC-GND 源地 YD-ESP32-S3**,并抓取其官方 README 与 Espressif 官方数据手册逐项核对。

> ⚠️ **上一版由此产生的具体错误主张(已全部作废)**:
> - ❌ "板载 CH334 HUB" —— 本板**没有** HUB(登记为 **V-14**,见 F-6)
> - ❌ "同一个 Type-C 接口下枚举出两个独立的 COM 口" —— 本板是**两个物理口**,不需要 HUB
> - ❌ "手机插上这块板的 Type-C 口后,浏览器里会出现两个设备" —— 插**一个**口只会出现**一个**设备
>
> ✅ **但上一版的工程结论仍然成立、且更硬**:两个 USB 通路**确实并存**,所以「**必须按精确 VID/PID 过滤、不能让用户在两个设备间盲选**」这条要求**不变**。它只是从"HUB 造成的同口双设备"改成了"两个物理口各挂一个设备"。

### F-4d-1 官方核实结果

| 项 | 核实结果 |
|---|---|
| 板子 | **VCC-GND Studio `YD-ESP32-S3`**(丝印 `HW678`);另有 Binghe 等**同设计不同品牌**的版本 |
| 模组 | **ESP32-S3-WROOM-1**;板子分 **HW-678A(N16R8:16MB Flash / 8MB PSRAM)** 与 **HW-678B(N8R2:8MB / 2MB)** 两档 |
| USB 口 1 | **原生 USB**,直连 **GPIO19 = D−、GPIO20 = D+**;官方称 *"ESP32-S3 USB OTG 接口,支持全速 USB 1.1"*,可烧录、通信、**JTAG 调试** |
| USB 口 2 | **USB 转 UART**,桥为 **CH343P**(沁恒 WCH),接 **UART0(GPIO43 = TX,GPIO44 = RX)** |
| 口数 | **两个物理 Type-C 座**,各自独立;**不是**一个口带 HUB |
| 供电 | 两口**任选其一或同时供电**;也可从 `5V`/`3V3` 排针供电 |
| 5V→3.3V | **LDO,1A**;另有**无线专用 LDO** |
| 按键 | **RST** 键(EN)+ **BOOT** 键。官方原文:*"boot 按键(配合 rst 按键可以引导进入 bootloader 模式,在复位后可以当做用户按键,就是 **GPIO0**)"* |
| RGB LED | **WS2812,由 GPIO48 驱动**。官方原文:*"配有一颗 WS2812-RGB LED(注意并不是通过 GPIO 直接点亮)"*、*"可寻址 RGB 发光二极管,由 GPIO48 驱动。型号为 WS2812"* |
| 实物特征(用户照片 + 官方图) | 四个灯连成一排:**RGB LED + 红色电源灯 + TX 灯 + RX 灯**;正面两组 `RGB` / `IN-OUT` 焊桥,背面一组 `USB-OTG` 焊桥 |
| 排针 | 两侧各 22 脚,**与 Espressif `ESP32-S3-DevKitC-1` 引脚定义一致**(仅间距宽 1 脚);官方:*"排针的序号与开发板原理图 (PDF) 一致"* |

**来源**(2026-09-21 抓取):
- [VCC-GND YD-ESP32-S3 官方 README](https://github.com/vcc-gnd/YD-ESP32-S3):双 Type-C / CH343P / WS2812 由 GPIO48 驱动 / 两套排针表 / BOOT 键为 GPIO0
- [Espressif — ESP32-S3-WROOM-1 & WROOM-1U 数据手册 v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf):Table 3-1 引脚表与注释 b/c、§4 Boot Configurations
- [Espressif — ESP32-S3-DevKitC-1 用户指南](https://documentation.espressif.com/esp-dev-kits/zh_CN/latest/esp32s3/esp-dev-kits-zh_CN-master-esp32s3.pdf):排针定义(GPIO45/46 标为 `FREE_1`/`FREE_2`)
- 用户实物照片(`HW678`、双 Type-C、引脚图丝印)

### F-4d-2 🔴 模组级引脚封锁(比板子身份更容易埋雷)

**这条直接约束引脚预算,必须单列:**

Espressif 数据手册 Table 3-1 注释 b 原文:

> *"For modules with **Octal** SPI PSRAM, i.e., modules embedded with **ESP32-S3R8** or ESP32-S3R16V, pins **IO35, IO36, and IO37** are connected to the Octal SPI PSRAM and **are not available for other uses**."*

| 结论 | 说明 |
|---|---|
| **GPIO35 / 36 / 37 在本设计(N16R8 ⇒ R8 ⇒ 八线 PSRAM)上不可用** | 它们**在模组内部已接 PSRAM**,不是"空闲但没引出"。⚠️ 本板排针上**照样有 `35/36/37` 丝印**(官方排针表也列了),**山寨板与本板都会把它们引出来** —— 这**不表示能用** |
| **GPIO33 / 34** | 模组引脚表**根本没有这两脚**(模组只到 `IO38`/`IO39`,且 `IO33/34` 未引出)。**本板排针图上也不存在 33/34** |
| 板子档位会影响可用性 | **HW-678A(N16R8,八线 PSRAM)封锁 35/36/37**;**HW-678B(N8R2,四线 PSRAM)则可用**并额外释放 `GPIO33–37` 共 5 根。⚠️ **这是 R-07「必须按最坏情况排布」的一个实例**:设计**按 N16R8 的封锁口径**,不依赖 N8R2 的额外收益 |
| 对现有设计的影响 | ✅ **无**。本设计从未使用 33–37(见 §2.3 可用池 `1,2,4–18,21,38–42,47,48`) |

> ⚠️ **另外两个模组级引脚属性**(`IO47`/`IO48` 注释 c):**仅当**模组搭载 `ESP32-S3R16V`(VDD_SPI=1.8V)时,GPIO47/48 的工作电压才是 **1.8V**,与其他 GPIO 不同。本设计用的是 **R8**,不适用;但若将来换模组,这句话必须重读。

### F-4d-3 本地指示载体(✅ 已核实,关闭 V-13)

| 项 | 结果 |
|---|---|
| 载体 | **板载 WS2812 可寻址 RGB LED** |
| 引脚 | **GPIO48** ✅ 已核实(官方原文明确) |
| 类型 | **WS2812 单线可寻址**(不是分立三色、不是简单 GPIO 直接点亮) |
| 同排其他灯 | 红色电源灯、TX 灯、RX 灯 —— ⚠️ **这三颗不是用户可控的 GPIO 灯**,不要把它们算进指示能力(电源灯恒亮;TX/RX 灯由串口活动驱动) |
| 影响 | 「设备侧本地状态出口」(NQ-02 剩余部分)的唯一载体 ⇒ **类型与引脚都已确定**,指示语可按"WS2812 可用颜色、但**不得依赖颜色**"来设计(理由见硬件文档 §2.7:颜色对色觉障碍者不可靠,且与"HUB 疑云"无关) |

**由此产生两条**必须在实现阶段处理的技术约束**(详见硬件文档 §2.7 与 F-6 的 V-15/V-16):
1. **WS2812 无硬件"灭"状态**:数据线停更时它**保持最后写入的颜色**。⇒ 「灭」只是一个被**写入**的状态,不能被当作"断电/崩溃"的同义词。
2. **驱动占用 RMT + 有严格时序**:WS2812 靠约 800kHz 的窄脉冲编码,通常用 RMT 外设发送;发送期间**不得被长临界区打断**。与射频事务的冲突处置见固件文档。

### F-4d-4 ⚠️ 山寨板风险(官方明示,会影响本产品)

VCC-GND 在其官方 README 里专门列了**山寨/翻新板的危害**,其中两条**直接命中本设计**:

| 山寨特征 | 对本设计的后果 |
|---|---|
| *"**2812 灯不焊接信号**,无法使用 2812"* | 🔴 **指示灯可能根本不亮** ⇒ NQ-02 的本地状态出口**静默失效**,而操作者会把"灯不亮"误读为"设备没在发射" |
| *"多处**丝印抄错**"*、*"没有原理图"* | 引脚丝印不可信 ⇒ **接线图不能按丝印照抄**,必须按官方排针序号核对 |
| *"使用第三方山寨模组…功耗高信号差、易死机"* | 与 R-07/R-01 的余量假设冲突 |

⇒ 已立为风险 **R-18** 与验证项 **TC-106(载板真伪/可用性抽查)**:必须**实机点灯**确认 WS2812 可用,不得因为"官方说 GPIO48"就假定手上这块能用。

### F-4d-5 对产品的三条直接后果(结论文本已按本次核实修正)

1. **两个 USB 通路并存,插一个口只出现一个设备。** 但用户**仍然可能插错口**(尤其是本产品自制的载板把它做成两个座时)。⇒ 官方丝印区分不可依赖,**必须在产品外壳上明确标识哪一个口是"控制口"**(硬件文档 §2.6)。
2. **设备过滤器仍不允许只用"通用 CDC"。** WebUSB 的 `requestDevice({filters:[{classCode:2}]})` 会把 CH343 那条通路一并选中。**必须用精确的 `vendorId`/`productId`**(原生 USB = Espressif `0x303a`;CH343 = WCH `0x1a86`)。⚠️ 本条**不因口数从一变二而放宽** —— 只要 CH343 通路在枚举中出现,盲选风险就在。
   > 原生 USB 的**确切 PID**,以及"CDC 设备标识是否稳定到可依赖",**不在本次核实范围内**,实现阶段实测(F-6 的 V-12)。
3. **走 CH343 等于每台产品多一颗桥。** CH343P 是**独立芯片**,量产时要在载板上为它留位置与成本;而原生 USB **零额外器件**且**不占 UART0**(GPIO43/44 本板已被桥占用,但控制通路不经过它们)。

---

## F-5 本机开发环境(2026-09-20 实测)

| 组件 | 状态 | 影响 |
|---|---|---|
| PlatformIO | 已装,**不在 PATH**:`~/.platformio/penv/bin/pio` | 构建前需 `export PATH` |
| espressif32 平台 | ✅ 已下载(`framework-espidf`、`framework-arduinoespressif32`、`toolchain-xtensa-esp-elf`) | **固件可真实编译与烧录** |
| ESP-IDF 独立安装 | ❌ 无 | 走 PlatformIO 即可,无需另装 |
| Flutter / Dart | ❌ **未安装** | 选原生 App 需先装 SDK(数 GB) |
| Node / npm | ✅ v22.22.1 / 已装 | PWA 路径零额外安装 |
| Python | ✅ 3.14.4,**无 pip** | 工具脚本只能用标准库 |
| git | ⚠️ **原文为"`/home/myf/DSH/Dev` 不是 git 仓库 —— 尚未纳入版本控制",该条已过时(2026-09-21 更正)** | 本仓库**已是 git 仓库**,位于 **`/home/myf/DSH/Dev/rf-lab`**(注意是 `rf-lab`,**不是** `Dev` 本身);文档变更现已按提交管理 |

---

## F-6 尚未核实、实现前必须补齐的项

| ID | 待核实内容 | 为什么重要 | 建议核实方式 |
|---|---|---|---|
| ~~V-01~~ | ~~AS10-M4463D-SMA 搭载的 SI4463 是 C2A 还是 B1B~~ | — | ✅ **已关闭(2026-09-20)**:官方手册给出模块级 **425–525MHz**;且 C2A/B1B 与频率无关。见 F-4c |
| V-02 | ~~混合内容规范的官方来源链接~~ ✅ **已完成** | — | 见 F-3(W3C + MDN 已引用) |
| V-03 | 中国大陆 433.92MHz / 2.4GHz 的发射限值与许可条件 | 决定发射白名单 | 见 `docs/60-compliance/`,须引用工信部原文 |
| V-04 | ~~DeepSeek JSON 输出模式的确切触发条件~~ ✅ **已完成** | — | 见 F-1b(三条件 + 空内容缺陷) |
| V-05 | 三个模块的实测 SPI 时序上限与 RSSI 读取时延 | 决定扫频速度 | 拿样机实测,不查手册 |
| V-06 | **局域网 `ws://` 在 HTTPS 页面下是否真被拦**(取决于浏览器对"same network"的宽容度) | 决定 PWA 能否连设备 | 实机实测 Android/桌面 Chrome,见 F-3 |
| V-07 | ~~ESP32-S3 在 **Wi-Fi + BLE 双跑**下的可用内部 RAM 余量~~ | — | ⏸ **本批不适用(2026-09-21)**:Wi-Fi/BLE 已移出本批范围,本批无协议栈争用内部 SRAM。**该实测推迟到无线能力重新纳入时**;届时"捕获缓冲上限"的余量口径需重算 |
| **V-12** | **走原生 USB 时,WebUSB 侧看到的 `vendorId`/`productId` 具体值**,以及该 CDC 设备标识是否**稳定可依赖** | **决定设备过滤器的写法**(F-4d-1 第 2 条)。而"过滤器写错"会让用户在**两个 USB 设备**之间盲选 —— 这是**本批唯一的配对通道**,选错即配对失败 | 真机枚举一次即可:插上后用 `navigator.usb.getDevices()` 打印 `vendorId/productId/productName`,与 CH343 那条并列对比。**与 TC-074a 同一次实测完成** |
| ~~V-13~~ | ~~板载 LED 的 GPIO 与类型~~ | — | ✅ **已关闭(2026-09-21 二次核实)**:板载 RGB LED = **WS2812,GPIO48**。见 **F-4d-3**;该节同时关闭了"类型不明"这一半 |
| **V-14** | 本板**是否真有** CH334 HUB(上一版据 Waveshare 另一块板断言有) | 若真有 HUB,则"一个口两个设备"成立,TC-074a 的断言 (a) 需改写;若没有(官方 README 与实物照片均无),则该断言作废 | 查[官方原理图](http://124.222.62.86/yd-data/YD-ESP32-S3/)(README 给出的资料包链接)或**实机插一个口再列一遍 USB 设备**。⚠️ 与 TC-074a 同一次实测完成 |
| **V-15** | **WS2812 的驱动占用(RMT channel)与刷新期间的时序纪律**,以及"数据线停更时保持最后颜色"这一行为在**本板**上是否如预期 | 直接决定 **`KILL` 指示能否可信** —— 若依赖"不写就会灭",就会把"设备崩了"显示成"一切正常" | 实测:写入某种颜色后**挂起驱动任务 / 触发看门狗复位**,观察 LED 是否保持颜色。见硬件文档 §2.7 |
| **V-16** | **载板是否为正品 YD 板**(山寨特征:`WS2812 信号未焊接`、丝印抄错、无原理图,官方 README 明示) | 🔴 山寨板上**指示灯可能根本不亮** ⇒ 本地状态出口静默失效,且操作者会把"灯不亮"误读为"未在发射" | 实机点灯 + 丝印/版本号核对(正版为 1.4 版)。见 **R-18** 与 **TC-106** |

---

## 复核触发条件

出现以下任一情况时,本文件必须重新核实并更新日期:

1. 距核实日期超过 **6 个月**
2. 决定切换 App 载体(**或重新把 iOS 纳入范围**)——注:iOS 已于 2026-09-20 移出范围,若重开需重跑 F-2 并与 §F-4b 的模块核实一并复核
3. DeepSeek 宣布模型改名或 API 版本变更
4. 决定增加/替换射频模块
5. 进入实现阶段前(作为门禁)
