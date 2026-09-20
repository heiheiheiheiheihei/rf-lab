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

**对产品的影响**(这是 Q-05 的核心):
- **纯 PWA 方案无法在 iPhone 上使用 BLE 近距控制**。场景 S2(近距 BLE 调试)对 iOS 用户不可用
- 但 **Wi-Fi/WebSocket 通道不受 Web Bluetooth 限制**,iOS 上仍可工作——**前提是解决混合内容限制**(见 F-3)
- 因此 iOS 用户的可行路径是:仅用 Wi-Fi 通道;或改用原生/包装方案获得 BLE 能力

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

## F-4 射频模块频率能力(芯片级,已核实)

| 芯片 | 实际频率能力 | 来源 |
|---|---|---|
| TI CC1101 | **300–348 / 387–464 / 779–928MHz**(三个分立子带) | [TI 产品页](https://www.ti.com/product/CC1101) |
| Silicon Labs SI4463 | **142–1050MHz**(C2A 版);B1B 版为 425–525MHz | [Si4463 规格页](https://cn.silabs.com/wireless/proprietary/ezradiopro-sub-ghz-ics/device.si4463?tab=specs)、[B1B vs C2A 区别](https://www.sekorm.com/news/79462573.html) |

**对产品的影响**:见 [ADR-0001](../adr/0001-frequency-coverage-gap.md)。需求的 119–1050MHz 无法由现有三模块覆盖。

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
| V-01 | AS10-M4463D-SMA 搭载的 SI4463 是 **C2A 还是 B1B** | 决定频段覆盖策略与 BOM | **向供应商书面索取模块规格书**(不要看电商页面的标称) |
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
2. 决定切换 App 载体或升级到 iOS 26+/新 Safari 版本
3. DeepSeek 宣布模型改名或 API 版本变更
4. 决定增加/替换射频模块
5. 进入实现阶段前(作为门禁)
