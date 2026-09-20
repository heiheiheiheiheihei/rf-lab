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

## F-5 本机开发环境(2026-09-20 实测)

| 组件 | 状态 | 影响 |
|---|---|---|
| PlatformIO | 已装,**不在 PATH**:`~/.platformio/penv/bin/pio` | 构建前需 `export PATH` |
| espressif32 平台 | ✅ 已下载(`framework-espidf`、`framework-arduinoespressif32`、`toolchain-xtensa-esp-elf`) | **固件可真实编译与烧录** |
| ESP-IDF 独立安装 | ❌ 无 | 走 PlatformIO 即可,无需另装 |
| Flutter / Dart | ❌ **未安装** | 选原生 App 需先装 SDK(数 GB) |
| Node / npm | ✅ v22.22.1 / 已装 | PWA 路径零额外安装 |
| Python | ✅ 3.14.4,**无 pip** | 工具脚本只能用标准库 |
| git | `/home/myf/DSH/Dev` **不是 git 仓库** | 本目录尚未纳入版本控制 |

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
| V-07 | ESP32-S3 在 **Wi-Fi + BLE 双跑**下的可用内部 RAM 余量 | 决定捕获缓冲上限与分包大小 | 实现阶段以 `esp_get_free_heap_size()` 实测 |

---

## 复核触发条件

出现以下任一情况时,本文件必须重新核实并更新日期:

1. 距核实日期超过 **6 个月**
2. 决定切换 App 载体(**或重新把 iOS 纳入范围**)——注:iOS 已于 2026-09-20 移出范围,若重开需重跑 F-2 并与 §F-4b 的模块核实一并复核
3. DeepSeek 宣布模型改名或 API 版本变更
4. 决定增加/替换射频模块
5. 进入实现阶段前(作为门禁)
