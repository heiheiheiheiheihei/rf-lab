# 手机 App 设计(Wi-Fi / BLE / USB 三通道)

状态: draft · 最后更新:2026-09-21 · 上游依据:

- `README.md`(项目定位、开发环境现状)
- `docs/README.md`(文档索引、阅读路线、**待决问题编号表**)
- `docs/00-overview/010-product-definition.md`(F-01..F-11、场景 S1–S5(**S1 已取消**)、成功判据 A-01..A-07)
- `docs/00-overview/040-verified-platform-facts.md`(**已核实平台事实 F-1..F-6,本文件的平台事实均引用它,不重复核实**)
- `docs/adr/0002-ai-analysis-in-app.md`(`accepted`:AI 分析放 App 侧)
- `docs/30-protocol/010-device-app-protocol.md`(`v1` + §7.1 v1.1 修订,**协议契约,权威来源,本文件不修改它**)
- `protocol/schemas/`(机器可读镜像,`v1.1`,36 个消息 schema + `common.schema.json`)

> 本文件是**设计文档**,不含任何实现代码。
> 平台事实的**唯一记录地是 `040-verified-platform-facts.md`**;本文只做引用与工程推论,不另起一套数字。
> 本文件对协议契约的意见**只报告不改动**,汇总见 §9。

---

## 0. 结论摘要

> **2026-09-20 范围决策(用户):放弃 iOS 支持,仅 Android。** 本节与 §1 已按此重写。iOS 相关分析保留在 §1.5,仅作为**决策依据的留档**,不再是设计输入。
>
> **2026-09-20 第二次决策:新增 USB 有线控制通道(F-11 / ADR-0003),产品面向屏蔽室。** 这给载体选型增加了一个**硬性新维度:WebUSB**。
>
> **2026-09-21 第三次决策:取消「无人值守远程部署」(`S1`)。** 设计前提收缩为"**操作者在场且与设备保持连接**"(产品定义 §2.4)。⚠️ **这看起来放宽了要求,实际相反** —— 依赖"App 不在场时设备自己工作"的所有设计被移除,而"**连接一旦消失就必须停止发射**"从"事后补救"升为首要防线(§2.4 与 §2.7)。

| 议题 | 结论 |
|---|---|
| 载体(回答 **Q-04**) | **推荐 PWA(Android)作为主载体**。仅 Android 后,**BLE 与 WebUSB 两个能力都不再有平台缺口**(Web Bluetooth 与 WebUSB 在 Android Chrome/Edge/Samsung/UC/Baidu 均可用),PWA 可覆盖 F-01..F-11 全部功能(含 F-11 USB 与配网全流程),且本机零新工具链(F-5) |
| 何时才需要原生 | 仅当需要:(a) **证书固定**;(b) **WebUSB 实测失败时的兜底**(见下方 ⚠️)。两者都不是当前 P0 的阻塞项。~~后台保活~~ **已随 `S1` 取消而不再是理由**(§2.4) |
| ⚠️ PWA 的唯一技术未知 | **WebUSB 能否访问 CDC-ACM 设备必须真机实测**(风险 USB-R1)。[caniuse](https://caniuse.com/webusb) 显示 WebUSB 全球 **77.89%**、**Chrome for Android 支持**;但移动端对部分设备类有限制的社区报告存在。**失败则降级为原生 Android USB Host 小壳**——只包一层 USB 读写,业务逻辑仍留在 Web 层 |
| 三通道 | **有线优先(USB) > 近距 BLE > 同室 Wi-Fi**;扫频/捕获进行中**不切换**(§2.3);**USB 可抢占无线**(契约 §2.2,是硬性优先级例外)。⚠️ Wi-Fi 的角色随 `S1` 取消由"远距"收窄为"**同室高吞吐**",其去留见产品定义 **NQ-04** |
| 保活 | 心跳 PING/PONG 5s(契约 §3.1);**前后台切换后必须整页重载重建连接**,不能只重连 socket(§2.4)。⚠️ 有线模式下整页重载**会丢失 WebUSB 授权**,处置见 §2.6 |
| 规模纪律 | 请求规模**必须**读 `CAPS.limits.ble_frame_max` / **`usb_frame_max`** 与 `max_edges`,不得硬编码 4096/3669(契约 §2.2.1、§2.2.2、§3.4) |
| 配对 | ✅ **本批首次配对只走 USB**(2026-09-21 定稿,NQ-01 方案 A),取回 `pairing_secret`(契约 §7.1)。USB 是物理端口,**结构上不需要 `pair_code`**;**BLE 在本批不承担配对职责** —— 收到配对请求须回 `pair_error:"no_display"`,**不得静默降级为免码配对**。**纯 Wi-Fi 从不是可信配对信道**。详见 §3.1 与产品定义 §8.1 |
| 离线 | **屏蔽室内手机可能无网** ⇒ DeepSeek 不可用,§5.3 的本地规则库降级**必须真的可用**,不能只是文档上的一句话 |
| AI | 分析在 App 侧调用 DeepSeek,设备只回传 `features`(ADR-0002);链路细节见 [`../50-ai/010-deepseek-analysis.md`](../50-ai/010-deepseek-analysis.md) |

---

## 1. 载体选型(仅 Android,Q-04)

### 1.1 对比表

> **iOS 列已移除**(C-06:放弃 iOS)。原三载体 × iOS 的完整对比留档在 §1.5,仅供追溯决策过程。

| 维度 | **PWA**(HTML/JS + Service Worker) | **Flutter** | **React Native / Expo** |
|---|---|---|---|
| BLE 能力(Android) | ✅ Web Bluetooth(`Bluetooth.requestDevice()`) | ✅ `flutter_blue_plus` | ✅ `react-native-ble-plx` |
| **USB 能力(Android)** | ⚠️ **WebUSB(`navigator.usb`)—— 需真机实测**(USB-R1) | ✅ 原生 USB Host API(`android.hardware.usb`)最稳 | ✅ 需第三方原生模块 |
| Wi-Fi WebSocket | ⚠️ 可用,但页面必须 HTTPS 且设备须 `wss://`(§1.3);除非实测确认局域网 `ws://` 被放行(V-06) | ✅ 无混合内容限制,可直连 `ws://` | ✅ 同左 |
| 开发环境成本(本机 F-5) | **最低**:Node v22.22.1 + npm 已装,**零新工具链** | **高**:Flutter/Dart **未安装**(数 GB);需 Android SDK | **高**:同左;Expo 需 `prebuild` 生成原生工程 |
| ~~后台保活~~(**已不是需求**,2026-09-21) | — | — | — |
| 原生能力 | 低:**无证书固定 API**(§1.3);蓝牙权限受浏览器管控;**无后台常驻**(⚠️ 但自 2026-09-21 起**不再需要**,见 §2.7 O-1) | 高:证书固定、前台服务、任意文件保存 | 高:同上 |
| 上架要求 | **无**(免安装、免审核);代价是进不了应用商店 | Google Play 审核(隐私清单、蓝牙权限说明) | 同左 |
| 迭代速度 | 服务端发版即生效,无需重新审核 | 需重新构建/发版 | 需重新构建/发版(Expo 可 OTA) |
| **推荐** | ✅ **主载体**(前提:WebUSB 实测通过,见 §1.2) | ⭕ 备选:必须证书固定 / **WebUSB 实测失败**时 | ⭕ 备选:同左 |

> **推荐结论为什么没有因为"多了一条 USB 通道"而改变**:WebUSB 与 Web Bluetooth 一样属于 **PWA 可用的 Web API**,且 Android 侧两者都受支持。因此"仅 Android ⇒ PWA 够用"这个推理**依旧成立**——只是**多了一个必须实测的前提**。
>
> **如果 WebUSB 实测失败怎么办**:不必整体切到 Flutter。可行且代价最小的路径是**只把那一段包成原生 Android 小壳**(USB 读写 + 把字节流转发给 WebView/本地服务),UI 与业务逻辑仍留在 Web 层。这样"零新工具链"的收益基本保住,只需引入 Android SDK 构建一个薄壳。

### 1.2 已核实的平台事实(Web Bluetooth)

完整表格与来源见 [`040-verified-platform-facts.md` F-2](../00-overview/040-verified-platform-facts.md)(来源:[caniuse — Web Bluetooth](https://caniuse.com/web-bluetooth),2026-09-20 抓取)。

**与本期(仅 Android)相关的部分:**

- ✅ **Android 侧无缺口**:Chrome for Android、Edge、Samsung Internet 6.2+、UC 15.5+、Baidu 13.52+ 均支持 `navigator.bluetooth`。这就是"仅 Android"使 PWA 直接够用的原因。

**不相关部分(留档):** Firefox(桌面与 Android)、Opera Mini、KaiOS 不支持;Safari(macOS)与 Safari on iOS/iPadOS 全版本不支持 —— 后者是放弃 iOS 的**直接原因**,分析见 §1.5。

工程推论:Web Bluetooth **只能在安全上下文(secure context)中使用**,受 `Permissions-Policy: bluetooth` 管控(默认 allowlist `self`),权限弹窗由 `requestDevice()` 触发且需要 [transient activation](https://developer.mozilla.org/en-US/docs/Glossary/Transient_activation)(用户手势)([MDN — Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API))。后果:**不能后台静默扫描**,每次连新设备都需要用户点击。**这一条与平台无关,仅 Android 也照样成立** —— 它决定了 §3.1 的配对流程必须由用户手势发起。

### 1.2.1 已核实的平台事实(WebUSB,新增)

来源:[caniuse — WebUSB](https://caniuse.com/webusb)(2026-09-20 抓取)。

| 浏览器 | WebUSB 支持 |
|---|---|
| Chrome(桌面) | ✅ 61+ |
| **Chrome for Android** | ✅ **支持** |
| Edge | ✅ 79+ |
| Samsung Internet | ✅ 8.2+ |
| UC Browser for Android | ✅ 15.5+ |
| Baidu Browser | ✅ 13.52+ |
| Opera Mobile | ✅ 80+ |
| Firefox(含 Android) | ❌ 不支持(Mozilla 立场为 *harmful*) |
| **Safari / iOS 全平台** | ❌ 不支持 |
| 全球使用率 | **77.89%** |

**与本期(仅 Android)相关的结论:Android Chrome 支持 WebUSB**,故 F-11 在 PWA 路径上**理论可行**。

⚠️ **但这不等于"已验证"**,必须区分三件事:

| 层级 | 状态 |
|---|---|
| 浏览器**声称支持** WebUSB | ✅ caniuse 与 Chrome 官方文档证实 |
| WebUSB 能在 Android 上**访问 CDC-ACM 设备** | ⚠️ **未验证**。社区有在 Android 上用 WebUSB 跑 CH340 串口的实践,但也有"移动端对部分设备类受限"的报告。**列为必须真机实测的第一项**(风险 USB-R1) |
| 我们的设备**(USB Serial/JTAG CDC)能被识别** | ⚠️ 未验证。设备侧是固定功能 CDC-ACM,VID/PID 为 Espressif;需确认 `navigator.usb.requestDevice()` 能枚举到它、且能 `claimInterface` |

**两个工程约束(与 Web Bluetooth 同源,必须遵守)**:

1. **安全上下文**:WebUSB 同样要求 secure context ⇒ 页面必须 `https://`。**这加重了 §1.3 的问题**:局域网 HTTP 页面不仅连不了 `ws://`,**也用不了 WebUSB**。
2. **用户手势 + 授权持久化**:`navigator.usb.requestDevice()` 必须由用户手势触发,且**授权与"页面会话/设备插入状态"绑定**。后果:
   - **不能后台静默连接**;
   - **整页重载(`location.reload()`)会丢失 WebUSB 授权** ⇒ §2.4 的"回前台整页重载"策略在有线模式下**代价更高**(需重新授权,若设备已插着通常一次点击即可)。
   - 因此 §2.6 给出有线模式下的**重连优先级**:先尝试复用既有 `USBDevice` 对象 → 失败再重授权 → 最后才整页重载。

### 1.3 HTTPS 页面 → `ws://` 局域网地址(F-3 / V-06)

**规范结论:被按混合内容拦截。** 依据:

| 依据 | 内容 |
|---|---|
| [W3C Secure Contexts §3.1](https://w3c.github.io/webappsec-secure-contexts/) | 「potentially trustworthy origin」只含 scheme 为 `https` / **`wss`**、主机落在 `127.0.0.0/8` / `::1/128`、以及 `localhost`(及 `.localhost`)。**`ws://192.168.x.x` 不在其中** |
| [W3C Mixed Content §3.2](https://w3c.github.io/webappsec-mixed-content/) | 非 potentially-trustworthy 且不可 upgrade 的请求一律 **blockable → 阻断**。WebSocket 不在可自动升级的 `image`/`audio`/`video` 名单内 |
| [W3C Bug 13526](https://www.w3.org/Bugs/Public/show_bug.cgi?id=13526) | 原文:*"you cannot connect to a `ws://` url from a `https://` based context"*(引自 F-3) |
| [WebSocket.org — WebSocket CORS](https://websocket.org/guides/troubleshooting/cors/) | 原文:HTTPS 页面「blocks connections to `ws://`」;并指出 WebSocket **不走 CORS**(无预检、无 `Access-Control-Allow-Origin`),搜到的「CORS 报错」几乎都是**混合内容阻断** |

**⚠️ 必须保留的细微之处(F-3 已记录,本文件据此调整口径)**:MDN 的措辞留了口子 —— *"most user agents now require a secure link for all WebSocket connections **unless they're on the same device or possibly on the same network**"*。也就是说,部分浏览器对**同一局域网**内的 `ws://` 目标**可能**放宽。因此:

- 本文件的立场是「**规范上被拦,实测可能因浏览器宽容度而不同**」,而**不是**「一定被拦」;
- `040-verified-platform-facts.md` 已把这件事登记为待核实项 **V-06**(在 Android Chrome 与桌面 Chrome 上实测 HTTPS 页面连局域网 `ws://` 与 `wss://`),并列入验证计划;
- **App 设计不得依赖 V-06 的结果** —— 必须按「会被拦」设计,把 `wss://` 当作主路径,这样无论实测结论如何都能工作。

由此得到的四条硬约束:

1. 契约 §2.2 默认地址是 `ws://<ip>:8266/v1`。**若 App 是 PWA,这条默认路径不能作为主路径**,必须走契约 §7 提到的 `wss://`。
2. `ws://localhost` / `ws://127.0.0.1` **豁免**(loopback 属 potentially trustworthy)。但「设备开 AP + 手机连过去」的 `ws://192.168.4.1` **不豁免**。
3. 契约 §7 写的「自签证书 + 指纹校验」**在浏览器里做不到**:浏览器没有暴露证书指纹固定 API(这是 **X.509 信任链**之外没有第二把尺子的结果),信任只能来自系统信任链。可行路径:(a) 用户手动把设备自签 CA 装进系统信任;(b) 设备用覆盖内网域名、由受信 CA 签发的证书(局域网裸 IP 的公信证书不现实,需配 split-DNS + 内网域名);(c) **App 走原生外壳**,由原生代码做证书固定 —— 这是原生载体相对 PWA 的**实质性**优势。此点已作为契约反馈登记(§9 第 1 条)。
4. 若为了绕开第 1 条而把 App 用 `http://` 提供,则页面本身不是安全上下文 → **Service Worker、PWA 安装、Web Bluetooth 全部失效**(且 F-3 已列为「牺牲 PWA 能力」的备选)。这是一个真实的二选一。

### 1.4 推荐结论(仅 Android)

**推荐:PWA(Android)作为唯一主载体。**

- **Android(Chrome/Edge/Samsung/UC/Baidu)**:PWA 可覆盖 F-07(Wi-Fi)+ F-08(BLE)+ 配网全流程;本机零新工具链(F-5);满足 A-01。
- **不再需要为覆盖 iOS 而引入原生外壳** —— 这是放弃 iOS 带来的最大简化:**原方案里"要不要为了 iPhone 的 BLE 去装 Flutter 工具链"这个数 GB 的决策,连同它的风险,一起消失了。**
- **原生理由现在只剩一条**(都不阻塞当前 P0,故不推荐现在就上):
  1. ~~**后台保活**~~ —— **已随 `S1` 取消而不再是理由(2026-09-21)**。原先它是"设备在车内/背包远置、App 息屏后仍要持续收 `SCAN_HIT`"派生的需求;无人值守取消后不存在这个使用方式(产品定义 §2.4)。**因此"PWA 必须让位给原生"的第一条理由消失了** —— 现在只剩证书固定一条(第 2 条)。
  2. **证书固定**:Android 上 PWA 同样没有证书指纹固定 API(§1.3),只有原生能做。若将来要求"设备证书不可被中间人替换"的强保证,则需要原生外壳。
- **决策留痕**:若将来重开 iOS,原分析见 §1.5,当时结论是"PWA 只能覆盖 iOS 的 Wi-Fi 模式,BLE 必须在 iOS 上缺失或另做原生壳"。

### 1.5 【留档】原 iOS 分析(已不适用,仅供追溯)

> 本节内容在 2026-09-20 的"放弃 iOS"决策前撰写。**它不是设计输入**,保留是为了:① 记录当时的依据;② 若将来重开 iOS,不必重新调研。

**结论:Web Bluetooth 在 iOS 上不存在任何可用路径。** 依据:

- [caniuse — Web Bluetooth](https://caniuse.com/web-bluetooth):Safari(macOS)与 **Safari on iOS/iPadOS 全版本 ❌**;Firefox(桌面与 Android)、Opera Mini、KaiOS 亦不支持;全局支持率 78.02%。
- **更根本的一环**:[App Store 审核指南 2.5.6](https://developer.apple.com/app-store/review/guidelines/) 强制"浏览网页的 App 必须使用相应的 WebKit 框架与 WebKit JavaScript"。iOS 上所有第三方浏览器都跑 WebKit,**因此没有一个能自带 Web Bluetooth** —— 换浏览器无解。
- 第三方 Safari 扩展 **iOSWebBLE** 桥接 CoreBluetooth 在 JS 层 polyfill `navigator.bluetooth`,但属**用户态扩展、非 WebKit 一部分**,caniuse 仍计为不支持。**不作为产品路径**。
- 当时的产品表述:「iOS = Wi-Fi/`wss` 模式可用,BLE 配网与近距 BLE 不可用;S2 场景对 iOS 用户不可得」。要让 iPhone 拿到 BLE 只能加原生外壳(Flutter/RN)。
- 另外记录一个当时影响保活设计的 iOS 缺陷:[WebKit Bug 308073](https://bugs.webkit.org/show_bug.cgi?id=308073)(2026-02-17):回前台后 `new WebSocket()` 可能**永久卡在 `readyState=0 (CONNECTING)`**,Safari 标签页与「添加到主屏幕」独立模式均复现,而 HTTP `fetch` 仍正常。当时据此定了"回前台 HTTP 通而 WS 不通时直接整页重载"的策略(§2.4 仍保留,因为 Android WebView/Chrome 也可能有类似粘滞状态,代价极低)。

---

## 2. 三通道连接管理

### 2.1 通道能力对照(契约 §2.2 / §2.2.1)

| 项 | Wi-Fi(WebSocket) | BLE(GATT) | **USB(CDC-ACM,新增)** |
|---|---|---|---|
| 地址 | `wss://<host>:8266/v1`(PWA 必须 `wss`;原生可用 `ws`) | 自定义 Service + Notify/Write | `navigator.usb.requestDevice()` 选设备 + `claimInterface` |
| 单帧上限 | `LEN` ≤ 65535 | **以 `CAPS.limits.ble_frame_max` 为准**(理论上界 3669B,建议 3600B) | **以 `CAPS.limits.usb_frame_max` 为准**(示例 16384;**无 FRAG 头**) |
| 吞吐 | 高(`SCAN_HIT`/`DATA_CHUNK` 受 §2.4 令牌桶限流,`stream_rate_hz` 默认 50) | 低,分片重组缺片 500ms 即弃整帧 | **高且稳定**(USB 2.0 全速;不受射频干扰) |
| 抗本机 2.4GHz 自扰 | ❌ 会被压掉 | ❌ 会被压掉 | ✅ **不受影响**(有线) |
| 角色 | **同室高吞吐**(原"远距离、无人值守、长时扫频(S1)",**S1 已取消** —— 见 §2.7) | 近距、免配网、调试(S2) | **2.4GHz 发射时的保底通道(S5)**、首次配对、桌面式调试 |

**规模纪律(必须遵守,否则会吃到 `frame_too_large` / `edge_clamped`)**:

- 单次 `CAPTURE_START.max_edges` 不得超过 `CAPS.limits.max_edges`;超出会被**钳制**并回 `WARN{edge_clamped}`,`CAPTURE_DONE.truncated=true`(契约 §3.4)。App 应**先读 CAPS 再决定请求值**,把钳制当异常路径。
- BLE 路径上单帧 `LEN` 不得硬编码 4096 或 3669,一律读 `ble_frame_max`;**USB 路径**同理读 `usb_frame_max`。超限设备回 `ERROR{frame_too_large}` 且**不静默截断**(契约 §2.2.1 / §2.2.2)。
- `DATA_CHUNK` 的分块上限是**承载相关**的,App 不得假设固定块大小(契约 §2.2.1)。

### 2.2 自动择优规则

App 启动与每次进入前台时**同时**探测各通道,10s 内按序择优:

| 优先级 | 条件 | 选中 |
|---|---|---|
| **0** | **USB 已插入且已授权**(`navigator.usb.getDevices()` 返回已授权设备) | **USB** —— **优先级最高** |
| 1 | 已绑定设备通过 BLE 在范围内且可连 | **BLE** |
| 2 | 已记住的地址可达且 `wss` 握手成功(≤5s) | **Wi-Fi** |
| 3 | 都不可达 | 进入「扫描设备」页,走 §3 配网/重连流程 |

> **为什么 USB 排在最前**(与契约 §2.2 的"USB 可抢占无线"一致):USB 连通意味着①带宽与延迟最好;②**不受本机 2.4GHz 发射干扰**。当用户插着线时,没有任何理由退回到会被自扰压掉的无线通道。这是**确定性规则,不是偏好**——不要为"省电"或"统一体验"而降级到无线。
>
> ⚠️ **一个必须做的例外**:USB 插入/拔出会改变活动链路。**若设备正在扫频/捕获,不要立即切**(见 §2.3);待任务结束再切,或明确提示用户本次数据作废。

补充:用户可在「设置」里把偏好固定为某一条(**推荐"固定 USB"**:插着线时没有任何理由退回会被自扰压掉的无线通道;原"`S1` 远程部署时强制 Wi-Fi"已随 `S1` 取消而失去意义 —— 见 §2.7),固定后不再自动择优。**不要做「按 RSSI 猜距离」的判断** —— 设备不做功率标定(产品定义 §3.1 非目标),用信号强度推距离是伪精度。

### 2.3 切换时的状态保持(关键策略)

契约 §5 规则 3 规定「发起新任务会隐式中止进行中的任务,并发 `WARN{preempted}`」——**切通道 = 切连接 = 会打断正在跑的扫频/捕获**。策略:

| 设备状态 | 是否允许切换 | 行为 |
|---|---|---|
| `IDLE` / `READY` | ✅ 允许 | 静默切换;新通道重做 `HELLO`/`GET_CAPS`;App 侧展示状态沿用 |
| `MODULE_READY` | ✅ 允许 | 同上;切换后**重新 `SELECT_MODULE`** 恢复活动模块(模块选择是设备侧状态,新连接不保证保留) |
| **`SCAN` / `CAPTURING`** | ❌ **禁止自动切换** | 保持当前通道直到任务结束;UI 把「切换」置灰并提示「扫频中,已暂停通道切换」。用户强切则先 `STOP_SCAN`/`CAPTURE_ABORT`,**明确告知本次数据作废**,再切 |
| 断线(被动) | — | 不切;按 §2.4 重连**同一条**通道,避免两个变量同时变化 |

> ⚠️ **唯一允许的例外**:当**无线链路被本机 2.4GHz 发射压掉**时(判据 A-07 的场景),若此时 USB 可用,**必须**切到 USB —— 因为那不是"优化",而是"当前通道已物理不可用"。固件侧同样允许 USB 抢占(契约 §2.2)。App 必须能识别这种状态并**显示"因本机发射暂不可用"**,而**不是**"设备离线"。

App 侧要维护「设备状态影子副本」,以 `CAPS` + 各 `*_STATE`/`SCAN_DONE` 为唯一更新源(契约 §1「能力协商优先」)。**不要假设切换后设备还记得之前的模块选择。**

### 2.4 心跳、断线与重连

- **心跳**:契约 §3.1,`PING{t_ms}` / `PONG{t_ms}`,间隔 **5s**;连续 **3 次**(15s)无 PONG 判为断线;RTT 同时用于链路质量指示。BLE 路径 15s 内无任何帧同样判为断线。
- **重连退避**:1s → 2s → 4s → 8s → 15s 封顶,无限重试但**每 5 次向用户报一次**。
- **前后台切换是最大的坑(已核实的证据)**:
  - **Android Chrome**(本期唯一目标平台):切后台/进入 Doze 后 WebSocket 会被断开,社区长期存在此问题([StackOverflow: Chrome/Android kills websockets running in background](https://stackoverflow.com/questions/55582103);PWA 场景 1005 断开与事件序列缺口有一手复现记录:[openclaw#2672](https://github.com/openclaw/openclaw/issues/2672))。
  - **通用防御**:监听 `visibilitychange`;回到前台若发现 WS 不可用,**先做一次 HTTP 健康探测**,若 HTTP 通而 WS 不通,直接 `location.reload()` 整页重载重建渲染进程,而不是无限重连。(此策略原为 iOS 的 WebKit Bug 308073 而设,见 §1.5;**对 Android WebView/Chrome 同样可能有粘滞 socket 状态,而整页重载的代价极低,故保留**。)
  - Service Worker **不能**用于保活:它本身会被终止,不适合长期持有 socket。
  - **Screen Wake Lock API 是 PWA 侧唯一现实的缓解手段**:可在**扫频进行中**申请屏幕唤醒锁避免息屏断连,且**只在前台有效**(Android Chrome 支持;`navigator.wakeLock.request('screen')`)。注意它会持续耗电并点亮屏幕,应在任务结束/切后台时主动释放。
- **对"App 不在场"的诚实结论(2026-09-21 更新)**:PWA **不适合**在 App 退到后台时继续工作(§2.4 的 Android 后台断连是平台事实)。原结论是"这意味着 S1 长时无人值守不可实现";**`S1` 已由用户取消**(产品定义 §2.4)⇒ 这个限制**不再是产品能力的缺口**,而变成一条**设计约束**:App 的使用方式一律是**前台 + 连线的交互式操作**,不存在"挂后台几小时等结果"的路径。**不得**再设计"后台攒数据、回来拉取"的流程,也**不新增**设备侧记录/回读(契约 §9 已列为非目标)。

### 2.5 USB 有线通道(F-11 / ADR-0003)——一次性解决三个老问题

USB 不只是一条"更快更稳"的通道。它同时关闭了本项目此前**绕了很久的三个问题**:

| 老问题 | 为什么被解决 |
|---|---|
| 2.4GHz 发射压掉本机 Wi-Fi/BLE ⇒ **发射中无法中止** | USB 是线,射频压不掉它。中止指令可送达(A-07) |
| "Wi-Fi-only 设备永远拿不到 `pairing_secret` ⇒ 无法发射" | **USB 与 BLE 同为可信配对信道**(契约 §7.1(5)),不再依赖 BLE 可用性 |
| 设备 AP 模式要猜 IP / 配网易失败 | 插线即用,不需要 IP、不需要 SSID、不需要路由器 |

**App 侧实现要点**:

| 项 | 做法 |
|---|---|
| 选择设备 | `navigator.usb.requestDevice({filters:[...]})` —— **必须由用户手势触发**(同 Web Bluetooth) |
| 已授权设备复用 | `navigator.usb.getDevices()` 返回**本会话内已授权**的设备;可用于"免再次弹窗"重连 |
| 接口 | 设备侧是固定功能 CDC-ACM ⇒ 需 `claimInterface()`;**若驱动已被系统占用可能抛错**,需捕获并给出可读提示 |
| 读写 | `device.transferIn()` / `transferOut()` 的 **Promise 版**(非事件回调版,后者已被弃用) |
| 帧层 | **与 Wi-Fi/BLE 完全相同**:同一套 6 字节帧头 + PAYLOAD;**无 FRAG 头** |
| 授权丢失 | 整页重载/刷新会丢授权 ⇒ 重连顺序:①尝试复用 `USBDevice` 对象 → ②`getDevices()` 找回 → ③重新 `requestDevice()`(需一次点击)→ ④最后才 `location.reload()` |
| 拔线检测 | 监听 `navigator.usb` 的 `disconnect` 事件 → 立即转为"通道丢失"UI,并按 §2.2 回落到 BLE/Wi-Fi |

**⚠️ 必须先验证的事(USB-R1,实现阶段第一项)**:

> **在 Android Chrome 上,`navigator.usb` 能否真的枚举并读写我们这个 CDC 设备。**
> 这不是"查文档能确认"的问题——WebUSB 的**设备类过滤**与 Android 的 USB 驱动占用情况都会影响结果。**验收方式**:一个最小 HTML 页面 + ESP32-S3 跑 USB Serial/JTAG echo,在真机上点"授权 → 收发改环"。
> **失败时的降级路径**(不必推翻 PWA 方案):只把 USB 那段包成**原生 Android 小壳**(`android.hardware.usb` USB Host API),把字节流转给 WebView 或本地 WebSocket;UI 与业务逻辑仍留在 Web 层。代价是引入 Android SDK 构建薄壳,但**不损失已有设计**。

### 2.6 有线模式下的重连(与 §2.4 的差异)

§2.4 对无线通道给出的策略是"回前台整页重载"。**有线模式不能照搬**,因为整页重载会丢 WebUSB 授权:

| 情形 | 无线通道(§2.4) | **有线通道** |
|---|---|---|
| 回前台发现通道不通 | 直接 `location.reload()` | ①先检查 `USBDevice` 是否仍在(`device.opened` / `getDevices()`);②重建传输(重新 `claimInterface` + 重做 `HELLO`);③失败才提示重新授权(一次点击);④**最后**才整页重载 |
| 心跳 | PING/PONG 5s | 同左。USB 通路极少丢包,**但 App 被系统挂起时 `transferIn` 不会执行**,故心跳仍是必要的判活手段 |
| 拔线 | 不适用 | `disconnect` 事件 → 立即切通道,不等心跳超时(比无线快得多) |

### 2.7 取消无人值守对 App 侧的约束(2026-09-21)

`S1 远程部署`取消后(产品定义 §2.4),使用方式被收敛为**一句话**:

> **App 前台 → 连着设备 → 操作者点着按钮 → 设备才动。**

由此产生四条**硬约束**(违反其一即视为设计缺陷):

| # | 约束 | 为什么 | 落点 |
|---|---|---|---|
| **O-1** | **所有功能必须前台可用**,不得依赖后台运行 | Android 后台断连是平台事实(§2.4),而"人在场"是本产品的前提 —— 没有需要后台的场景,所以也不必与平台对抗 | 全局 |
| **O-2** | **禁止"后台攒数据、回前台拉取"的流程**;不得为此新增设备侧记录/回读 | 该流程唯一的存在理由是"App 不在场",而它已不是场景(契约 §9 非目标) | §2.4、契约 §9 |
| **O-3** | **连接消失 ⇒ 必须立即停止发射**,且 App 必须让用户看到这一因果 | 取消无人值守后,"操作者在场且连着线"是发射的**充分前提**;连接没了前提就没了。⚠️ **不能把断连当作"稍后会重连,再等等"** | §2.4、固件文档 §9、**TC-054** |
| **O-4** | 断连提示必须区分"**设备离线**"与"**因本机发射暂不可用**" | 前者要排查设备/网络,后者只需等发射结束或改插 USB。混为一谈会让用户去解决一个不存在的问题 | §2.3、A-07 |

> ⚠️ **最容易搞反的一点**:取消无人值守**像是**放宽了要求(不用管挂机了),实际是**收紧了** —— 它把"设备必须在有人看着的时候才发射"从一句设计意图变成了**可以被测试的硬要求**(TC-054 由 P2 升为必过项)。**不要**因为"反正有人在场"就删掉超时/看门狗/断连归 idle 的实现。

---

## 3. 连接与配对流程

### 3.1 首次配对与配网

契约 §7.1(v1.1)已给出**可实现的配对定义**,与原文「首次配对绑定的长期密钥」配套:

```
设备开机若未配对:生成 32B 随机 pairing_secret(硬件 RNG)
  → 存 NVS(不可读回)
  → 同时生成 6 位一次性配对码 pair_code  ⏸ 本批无显示载体(OLED 延期,见产品定义 §8)
App   → HELLO{client_id, pairing:true}            # USB:不带 pair_code
设备  → HELLO_ACK{device_id, pair_ok:true, first_bond:true, pairing_secret:"<32B base64url>"}   # 仅 USB
   或  HELLO_ACK{pair_ok:false, pair_error:"bad_code"|"no_display"|"already_bound"|"ble_only"|"rng_failed"}
```

> ✅ **2026-09-21 定稿:本批首次配对只走「路径 U(USB)」(NQ-01 方案 A,用户决策)。** `pair_code` 需要一条**与 BLE 不同源**的搬运通道,而本批没有任何屏/标签/灯光协议可用;USB 是物理端口,连接行为本身就是"在场证明",**结构上不需要 `pair_code`**。
>
> **路径 A(BLE 配对)在本批不是"保留设计"而是"明确不可用"**:无搬运通道时设备**必须**回 `pair_error:"no_display"`。⚠️ **App 必须据此把「蓝牙配对」入口隐藏或置灰**,只留「用 USB 线配对」—— 放一个必然失败的按钮会诱导用户反复重试,并把 `no_display` 误读成设备故障。

**路径 U(本批唯一路径):USB 有线配对**

1. 用户用 **OTG 数据线**把手机与设备连起来(设备自行供电,不依赖手机 OTG 供电)。
2. App 经 **WebUSB** 拿到 CDC-ACM 接口(`navigator.usb.requestDevice()`,须用户手势)。
   ⚠️ **WebUSB 能否访问 CDC-ACM 是整条有线路径的唯一技术未知**(USB-R1),**实现阶段第一项真机实测**(TC-074a);失败则降级为原生 Android USB Host 小壳。
   ⚠️ **2026-09-21 起该项的严重度升级**:USB 现在是**唯一**的配对路径 ⇒ 它不通则**首次配对无路可走**(不再是"少一条通道")。
3. App 发 `HELLO{app_version, proto_min, proto_max, client_id, pairing:true}`(**不带 `pair_code`**)。
4. 设备在 **USB 链路**上回 `HELLO_ACK{..., pair_ok:true, first_bond:true, pairing_secret}`。✅ **Q-09 已关闭**:USB 已被定为可信配对信道,App 不再需要为"USB 配对是否被允许"准备回退。
5. 其余步骤与路径 A 第 5–9 步相同(`GET_CAPS` / 对齐 `last_accepted_seq` / `SET_NET` / 写设备档案)。
6. **本路径同时缓解配网问题**:USB 是线,不必猜 AP 的 SSID 与 IP(这原本是 OLED 的第二项职责)。
   ⚠️ **但请勿过度承诺**:契约目前**没有"读出设备当前 IP/SSID"的字段**(§9.1/契约 §9.1)。在补齐前,USB 只能保证"配对不需要猜地址",**不能保证"UI 能显示设备当前地址"** —— 后者需 v1.2 加字段。

**路径 A(⏸ 本批不可用:BLE 在无 `pair_code` 载体时不得配对)**

1. 用户点「添加设备」→ 点**「扫描」**(Web Bluetooth 必须由用户手势触发,§1.2)。
2. 浏览器弹出设备选择器;用户选中设备(设备名含 `RFLAB-xxxx`)。
3. App 建立 GATT 连接 → 读**设备端的 6 位一次性配对码**(⏸ 本批无载体;下一批为 OLED 屏)→ 发 `HELLO{app_version, proto_min, proto_max, client_id, pairing:true, pair_code}`。
4. 收 `HELLO_ACK{fw_version, proto, device_id, chip, uptime_ms, pair_ok:true, first_bond, pairing_secret}`;App 经 BLE 加密链路接收 `pairing_secret` 并只存本地。配对码的作用是**防中间人**(BLE 配对若被截获,攻击者拿不到设备端的码)。
5. `GET_CAPS` 拿 `CAPS`,校验 `proto` 与 `proto_min/max` 有交集;无交集 → 提示升级并终止(契约 §8)。
6. **对齐令牌基线**:把 `HELLO_ACK.last_accepted_seq` 存入设备档案,本地 `seq := max(本地值, last_accepted_seq)`(**这是防止 App 重装后被永久拒绝发射的关键**,契约 §7.1(1.2))。
7. 展示 Wi-Fi 表单 → 发 `SET_NET{ssid, pass}`。
8. **设备入网后目前不会主动回报结果** —— 契约 §3.8 只有 `SET_NET` 单向消息,**没有对应的 ack**(见 §9)。降级做法:发完后在 60s 内轮询 `GET_STATUS`,看 Wi-Fi RSSI 是否变为有效值(字段名待定义,见 §9),或直接对候选地址做 `wss` 握手探测。
9. 握手成功 → 写设备档案(`device_id` + `pairing_secret` + `last_accepted_seq` + 上次地址 + 通道偏好)进 IndexedDB。
10. **重新配对需设备端按键确认**(契约 §7.1(1)),App 不得提供「远程一键重新配对」。

**配对失败的 UI 处置**(契约 §7.1(1.1),必须区分——否则用户会反复重试一个永远失败的动作):

| `pair_error` | App 文案与动作 |
|---|---|
| `bad_code` | ⚠️ **本批不会出现**(USB 不校验码;BLE 在取到码之前就回 `no_display`)。**不要为它写路径,也不要为它写测试** —— 留待下一批 OLED 到位后启用 |
| `no_display` | 「**本设备当前没有可显示配对码的屏幕,请改用 USB 数据线连接完成配对**」——**不要**提示重试;附带"下一步该插哪根线"的引导。⚠️ 由于本批 BLE 入口应已置灰,该错误主要出现在"用户从旧版本/其它客户端发起 BLE 配对"时,**文案仍必须准确**,不得渲染成设备故障 |
| `already_bound` | 「该设备已与另一部手机绑定。如需改绑,请在**设备端按键**确认后重新配对」——**不要**提示重试;⚠️ **插线不能替代按键**(USB 在场性不覆盖改绑场景) |
| `ble_only` | ⚠️ **本批语义 = 「首次配对请改用 USB 数据线」**,不再是"改用蓝牙" —— BLE 在本批同样不能配对。App 文案必须同步改口,否则会把用户引向一条必然失败的路 |
| `rng_failed` | 「设备安全模块异常,拒绝配对」;不得降级为弱随机,引导联系维护 |

**路径 B(备选):Wi-Fi AP 直连** —— 当 BLE 与 USB 都不可用时使用

1. 用户按设备说明把设备切到 AP 模式(硬件按键/上电窗口);⏸ **本批无屏,SSID 与地址必须靠固定规则**(SSID 含 `device_id`,地址固定 `192.168.4.1`),写进用户文档;下一批由 OLED 显示。
2. 在**系统设置**里连上设备 AP(浏览器无法代劳,此路径不涉及 `SET_NET`)。
3. App 里手输/确认设备地址(默认 `192.168.4.1`)→ `wss` 握手。
4. 证书不受信任时,Android 会直接失败 → 需要「用户安装设备 CA」的一次性引导页(§1.3 第 3 条);或改走原生载体做证书固定。
5. ⚠️ **此路径下 `pairing_secret` 无法建立**(契约 §7.1 明确**只经可信带外链路下发一次**,2026-09-21 起 = **仅 USB**;Wi-Fi 与 BLE 路径不下发)→ 该设备**不具备发射授权能力**(`tx_requires_token=true` 时 `TX_REQUEST` 必然 `tx_bad_token`)。届时应把发射页设计为**只读**,并在 UI 上说明原因,而不是让用户反复失败。**本批的引导话术应直接指向"插 USB 线"**,因为那是本批确实可用的路径。

> **不要用屏幕/标签传密钥。** 一个看起来诱人的做法是"让设备显示配对码/二维码,用户输入后 Wi-Fi 路径也能拿到 `pairing_secret`"。**这不成立**:`pairing_secret` 一旦能经"人眼可读"这条带外信道传递,就意味着**不在设备旁边的人也能拿到它**(照片、录屏、远程视频),而本项目的红线之一是"远程可控但发射必须受在场约束"。因此 §7.1 的"只经 USB"不是实现偷懒,而是**有意的信任边界**:物理端口天然要求在场。若将来确实要开放 Wi-Fi 配对,必须先在合规文档中重新评估这条红线是否仍然成立。
>
> ⏸ **对"标签静态码"方案(B)的同一批评**:若采纳"出厂随机码印在机身标签",它虽然是带外信道,但**码是静态的**——设备被拍照/被短暂接触即永久泄露该码,防中间人强度显著下降。它**只能作为临时兜底**,且应在文档中标注这一弱点(见产品定义 §8)。

### 3.2 之后的重连

记住设备后,App 每次启动:① 尝试 BLE 直连(Android PWA 支持)→ ② 否则对「上次地址 + mDNS 名」做 `wss` 探测 → ③ 都失败则提示「设备不在线」并允许手动改地址。**不重复走配对流程。**

---

## 4. UI 信息架构

### 4.1 页面清单

| 页面 | 主要元素 | 关键交互 |
|---|---|---|
| **① 设备连接 / 配网** | 设备卡片(展示名、`device_id`、通道徽章、RTT)、添加设备、忘记设备、证书状态、配对状态 | 点击连接;BLE 扫描需用户手势;证书不受信任时给引导;`pair_code` 输入框(⏸ **仅下一批 OLED 到位后才需要**;本批主入口是**「用 USB 线配对」按钮**,失败时按 `pair_error` 分支引导) |
| **② 模块管理(S4)** | 模块列表(`slot`/`chip`/`present`/`bands`/`tx_max_dbm`/`tx_allowed`/`driver_version`/`spi`/`ant`)、活动模块高亮 | 点选 → `SELECT_MODULE`;`present=false` 置灰;切模块前警告「会中止当前任务」(契约 §3.2)。**`spi.bus` 三模块相同是正常的**(单 SPI2 + 独立 CS),**只有 `cs` 冲突才是配置错误** —— 不得把「bus 相同」当异常报警(契约 §3.2) |
| **③ 扫频(F-01/F-02)** | 起止频率(受 `CAPS.bands` 夹取)、步进、驻留、`max_edges`、阈值滑杆、**频谱条 + 瀑布图**、命中列表、进度、**真实覆盖区间**、命中完整性提示 | 开始/停止;点命中项 → 捕获页;`WARN{band_clipped}` 时把未扫区间画灰;**`hits_dropped > 0` 必须提示「本次明细不完整」并给「取回全部命中」按钮**(`GET_SCAN_HITS` 分页) |
| **④ 捕获与波形(F-03/F-04)** | 波形(RLE 展开)、`edges`/`duration_ms`/`truncated` 徽章、**特征面板**(§3.6 全字段,**含 `*_confidence`、`raw_semantics`、`incomplete`**)、解码结论 | 捕获/中止;波形缩放;`truncated=true` 提示「已达 max_edges 上限」;`incomplete=true` 时必须**显眼标注「数据不完整」** |
| **⑤ AI 分析(F-09)** | 分析卡片:`signal_type`/`protocol_guess`/`confidence`/`threat_level`、`matched_protocols`、`recommended_actions`、`caveats`、「已发送内容」折叠区 | 点「分析」上传特征;可展开查看**实际发送的 JSON**;置信度低标黄 |
| **⑥ 信号库** | 已保存信号(名称、频点、调制、来源:本地/设备)、特征预览 | `SAVE_SIGNAL` / `LIST_SAVED` / `DELETE_SAVED` |
| **⑦ 发射(F-05/F-10)** | 目标参数、**四道门禁**校验结果、二次确认区、`TX_STATUS` 结果 | 见 §5(b);`tx_whitelist` 为空 → 整页只读并显示「本机未授权任何发射频段」;无 `pairing_secret`(Wi-Fi-only 路径,§3.1 路径 B)→ 同样只读 |
| **⑧ 设置与审计** | 链路偏好、DeepSeek Key 管理、同意书、**发射审计镜像**、数据清理 | 导出审计(JSON/CSV);清理缓存;切换模型 |

### 4.2 扫频结果如何可视化

三种视图并存、**共享同一份命中数据**(不要各自维护状态):

1. **频谱条(主视图)**:横轴频率、纵轴 RSSI;`rssi_threshold_dbm` 画水平参考线;命中点用强调色。数据源 `RSSI_SAMPLE` 与 `SCAN_HIT`。
2. **瀑布图(时序)**:横轴频率、纵轴时间、颜色映射 RSSI;每次 `SCAN_PROGRESS`/`SCAN_HIT` 追加一行。用于分辨「持续载波」与「周期性突发」——这正是 AI 威胁分级要用的直观线索。
3. **命中列表(表格)**:`f_hz`、`rssi_dbm`、`t_ms`、**与噪声底之差**。可排序、可按频点聚合(同频多次命中合并计数)。**这一列是与噪声底之差,不是绝对 RSSI** —— 设备不做功率标定。

**三条诚实性要求**(全部有契约条文支撑):

- 显式区分「扫过的区间」与「请求但未覆盖的区间」;`SCAN_DONE.coverage` 一到就重绘覆盖条。**不得**把未扫区间画成「无信号」(契约 §3.3)。
- **`hits_dropped > 0` 时必须提示「本次明细不完整」**,避免把「没推送」误读为「没信号」;需要明细时用 `GET_SCAN_HITS{scan_id, offset, limit}` 分页取回,命中缓存在设备端保留到**下一次扫描开始**(契约 §3.3、§2.4)。
- 所有命中均以 `scan_id` 归属;切换扫描后旧 `scan_id` 的明细不可再取。

### 4.3 发射功率的展示与调节(C-11 + C-12,2026-09-21)

出厂默认是**每个模块各自的满功率**,但"满功率"**不是一个统一数字** —— 这一节的核心任务就是**不要让用户以为设备在擅自降功率**。

**唯一正确的口径:实际生效功率由设备回报,不由 App 计算。**

| 模块 | 模块 `tx_max_dbm`(硬件) | 出厂默认(满功率) | 能否调到 20dBm |
|---|---|---|---|
| M1 AS10(SI4463) | 20 | **20dBm** | ✅ |
| M2 AS07(CC1101) | **10** | **10dBm** | ❌ **模块硬件所致,不是策略限制** |
| M3 AS01(nRF24L01P+PA) | 20 | **20dBm** | ✅ |

- `policy.tx_power_cap_dbm` = **20(设备级全局最大值)**。有效上限 = `min(请求值, 20, 该模块 tx_max_dbm)`。
- **App 不得自己算完就显示**:必须显示 `TX_STATUS.granted_power_dbm`(契约 §3.7)。App 本地计算只用于**即时反馈**,最终以设备回报为准。
- ⚠️ **文案要求(与 A-06 同源)**:当用户要求 20dBm 而当前模块是 M2 时,提示必须是
  > 「**实际 10dBm** —— 当前模块(AS07)硬件上限为 10dBm,与功率设置无关;换用槽位 1/3 可达 20dBm」
  **禁止**写成「功率被设备限制/被策略拒绝」 —— 那会让用户跑去翻设置找一个不存在的开关。
- **调节控件**:发射页与设置页各有一处;步进 ≤ 1dB;显示 `请求值 → 生效值` 两个数(相同时只显示一个);到达模块上限时滑杆**止于 `tx_max_dbm`**,而不是止于 20。
- **降低功率不需要额外确认,提高才需要**:把功率提到当前模块上限需要在二次确认弹窗里**显式显示生效值**。理由:降低功率是**唯一不需要换硬件就能缓解自扰(R-01)与近场饱和的手段**(近场饱和会出现在"发射端紧贴接收端"的回放测试里,表现是**明明发了却收不到**——这一条应写进用户文档)。
- **审计镜像必须同时记 `power_dbm`(请求)与 `granted_power_dbm`(生效)**:否则事后无法判断某次发射是否发生过夹取。

---

## 5. 关键交互流程(时序图)

### (a) 扫频 → 命中 → 捕获 → 特征提取 → 上传 AI → 展示分析

```
 设备(固件)                 App(PWA / 原生)                      DeepSeek API
    │                             │                                    │
    │◄──── START_SCAN ────────────│  用户:设频段/步进/阈值/max_edges
    │──── SCAN_HIT{scan_id} ×N ──►│  命中列表追加 + 频谱条高亮 + 瀑布一行
    │──── SCAN_PROGRESS ×N ──────►│  进度环;覆盖条区分「已扫/未覆盖」
    │──── WARN{band_clipped} ────►│  覆盖条把越界区间画灰 + 文案提示
    │──── SCAN_DONE ──────────────►│  hits_total / hits_pushed / hits_dropped
    │                             │  hits_dropped>0 → 提示明细不完整 +
    │◄──── GET_SCAN_HITS ─────────│  「取回全部命中」(分页)
    │──── SCAN_HITS{offset,hits} ►│  合并进命中列表(按 scan_id 归属)
    │                             │
    │◄──── CAPTURE_START ─────────│  用户点命中项 →「捕获」
    │──── DATA_CHUNK(二进制) ────►│  校验 MAGIC/VER/SEQ/FRAG;波形增量绘制
    │──── CAPTURE_DONE{features} ►│  特征面板填充(含 *_confidence/incomplete)
    │                             │  ① 规范化 features → feature_hash
    │                             │  ② 查本地分析历史(命中则直接展示,不再上传)
    │                             │
    │                             │──── POST /chat/completions ───────►│
    │                             │     response_format=json_object     │
    │                             │◄──── 严格 JSON ────────────────────│
    │                             │  ③ JSON 校验 → 通过 → 分析卡片
    │                             │     校验失败 → 纠错重试 1 次
    │                             │     仍失败 → 降级纯文本展示
    │                             │  ④ 写入本地分析历史(feature_hash 索引)
    │◄──── SET_RSSI_STREAM ───────│  用户:对结论点「定点观测」
```

### (b) 从 AI 结论发起发射:四道门禁 + 令牌校验

```
 用户                  App                                   设备
  │ 点「按建议发射」    │                                     │
  │────────────────────►│                                     │
  │                     │ 门禁① 频段白名单:CAPS.policy.tx_whitelist
  │                     │        含 f_hz?(空 = 禁止一切发射)
  │                     │ 门禁② 功率:power_dbm ≤ tx_power_cap_dbm
  │                     │        且 ≤ module.tx_max_dbm
  │                     │        (出厂默认已 = 模块上限 ⇒ 此门只在用户主动调高越界时触发)
  │                     │ 门禁③ 用户显式确认(下)
  │                     │  任一不过 → 本地即拒,**不发帧**
  │ 二次确认弹窗 ◄──────│  展示:频点/调制/功率(**请求值→生效值**)/重复次数/内容摘要
  │                     │        + 触发它的 AI 结论与 caveats
  │ 勾选「已获授权」    │
  │ 长按 3s 确认 ──────►│  门禁③ 完成
  │                     │ 门禁④ 授权令牌(契约 §7.1):
  │                     │   token = HMAC-SHA256(pairing_secret,
  │                     │       device_id|seq_be32|f_hz_be64|mod|
  │                     │       power_dbm_x10_be16)
  │                     │   token_wire = base64url(token[0:16])
  │                     │── TX_REQUEST{…, auth_token} ────────►│
  │                     │       设备:重算 HMAC + 常数时间比较    │
  │                     │       + seq 严格递增 + uptime 相对窗口 │
  │                     │◄── TX_STATUS{state:"armed"} ─────────│
  │                     │ 写本地审计镜像(含被拒的)              │
  │                     │── TX_REQUEST(确认发射) ─────────────►│
  │                     │◄── TX_STATUS{state:"sent"|"denied"} ─│
  │ 结果卡片 ◄──────────│   设备侧写环形日志(契约 §7)          │
```

> 图中 `armed` → `sent` 的两段式**是契约已列出的 `TX_STATUS.state` 取值**(`denied|armed|sent|failed`),但契约**未定义** `armed` 的触发条件、有效期与是否需要第二次 `TX_REQUEST`(见 §9 第 5 条)。在契约补齐前,App 按「`armed` 有效期内再次 `TX_REQUEST`」实现,并把该假设写进代码注释与待办。
>
> 门禁的顺序要点(契约 §7.1(4)):前三道**任一不过即拒**,且**不因令牌有效而放宽**。App 侧做①②③是**为了给用户即时反馈**,真正的强制点在设备侧 —— App 的本地拒绝**不能**替代设备校验。

---

## 6. 本地数据模型

### 6.1 存储选型

| 数据 | 存储 | 理由 |
|---|---|---|
| 设备档案、设置、同意书 | `IndexedDB`(**不用 localStorage**) | 结构化、可索引;localStorage 是同步 API 且容量小 |
| `pairing_secret`、DeepSeek API Key | `IndexedDB` + WebCrypto 非可导出密钥包装 | 目标是「不落明文」;但纯 Web 环境**做不到真正的密钥保护**,见 §8.3 |
| 信号库(特征 + 可选原始码流) | `IndexedDB`,**原始码流用 Blob 分表存** | 1000 跳变 ≈ 3KB(契约 §3.5),几百条即 MB 级,不能塞进一条记录 |
| AI 分析历史 | `IndexedDB`,唯一索引 `feature_hash` | 去重与「不重复付费」直接依赖它(见 AI 文档 §5.2) |
| 发射审计镜像 | `IndexedDB`,**append-only** | 与设备侧环形日志互为副本;导出 JSON/CSV |
| 波形/瀑布渲染中间态 | 内存 only | 可重建,不值得持久化 |

### 6.2 数据保留与清理

| 数据 | 默认保留 | 清理策略 |
|---|---|---|
| 分析历史 | 500 条 / 90 天 | 先到者先删;`feature_hash` 索引同删 |
| 原始码流 | 100 条 / 30 天 | 可设「不保存原始码流」只留特征 |
| 审计镜像 | **不自动删除** | 只能由用户显式导出后清理;清理前二次确认 |
| 设备档案 | 直到用户「忘记设备」 | 忘记设备时一并删除 `pairing_secret`(此后需设备端物理按键重新配对) |

提供「一键清除全部本地数据」;必须在 UI 说明**设备侧环形日志不受本地清理影响**(契约 §7 审计在设备侧)。

---

## 7. 状态与异常处理(覆盖契约 §6 全部错误码)

| code | 用户可见文案 | App 恢复动作 | 自动重试 |
|---|---|---|---|
| `not_ready` | 「设备尚未完成握手」 | 静默重发 `HELLO`;连续 2 次失败 → 断开并走重连 | ✅ |
| `version_mismatch` | 「设备固件与 App 协议版本不兼容,请升级」 | 终止会话,展示双方版本号与升级指引 | ❌ |
| `module_absent` | 「该射频模块未检测到,请检查是否插好」 | 重新 `LIST_MODULES` 刷新模块页;**不要自动切到别的模块** | ❌(可手动重试) |
| `module_busy` | 「模块正忙」 | 先 `STOP_SCAN`/`CAPTURE_ABORT`,500ms 后重试 1 次 | ✅ 1 次 |
| `band_out_of_range` | 「该频率超出当前模块能力」 | 用 `CAPS.bands` 夹取,并把输入框改写成夹取后的值(让用户看到改了什么) | ❌ |
| `frame_too_large` | 「本次请求超出当前链路单帧上限」 | 读 `CAPS.limits.ble_frame_max` **降低请求规模后重发**;**不得重试同尺寸**(契约 §2.2.1、§6) | ❌(需改参数) |
| `band_clipped`(WARN) | 「部分频段超出模块能力,实际只扫了 X–Y」 | 覆盖条灰显未覆盖区间;以 `SCAN_DONE.coverage` 为准 | — |
| `edge_clamped`(WARN) | 「捕获点数超过上限,已按 N 点执行」 | 读 `CAPS.limits.max_edges` 后重发;`truncated=true` 时同时标注数据被截断 | — |
| `throttled`(WARN) | 「数据速率超限,已丢弃部分命中/数据」 | 结合 `hits_dropped` 提示明细不完整;提高 `dwell_ms`/缩小频段/降 `max_edges` | — |
| `tx_denied` | 「发射被设备拒绝:<detail>」 | **展示原因,不得重试**(契约 §6 明确) | ❌ |
| `tx_bad_token` | 「授权令牌无效或已过期」 | 重新走一次 §5(b) 的门禁③④,**重算令牌并递增 `seq`** | ❌(需用户确认) |
| `frag_timeout` | 「数据分片丢失,本次捕获不完整」 | 重新发 `CAPTURE_START`;BLE 上建议同时降 `max_edges` | ✅ 1 次 |
| `hw_error` | 「射频硬件异常(SPI/RMT)」 | 提示重启模块;自动重试**仅 1 次**,仍失败则要求用户干预 | ✅ 1 次 |
| `unsupported` | (不打扰用户) | 按契约 §1 忽略该消息,记一条调试日志 | — |

补充的**非协议错误**:

| 场景 | 文案 | 动作 |
|---|---|---|
| `wss` 握手失败/证书不可信 | 「无法建立安全连接:证书不受信任」 | 引导安装设备 CA;或提示改用原生外壳(§1.3) |
| 无 `pairing_secret` 却尝试发射 | 「本机未完成安全配对,无法发射」 | 引导走 BLE 配对(§3.1 路径 A);Wi-Fi-only 路径下说明该能力不可得及其原因 |
| DeepSeek 401 / 402 / 429 / 5xx | 分别「Key 无效」/「余额不足」/「请求过快」/「服务暂时不可用」 | 见 AI 文档 §4.4 |
| 回到前台后 WS 不通但 HTTP 通 | (不提示) | 直接 `location.reload()`(§2.4) |

---

## 8. 安全

### 8.1 设备绑定与防串台

- 绑定依据:`HELLO_ACK.device_id` + `pairing_secret`(契约 §7、§7.1)。一台设备只与一个 `client_id` 配对;重新配对需设备端物理按键确认。
- 防串台:App 在 UI 与所有本地记录里以 `device_id` 为主键,**不按 IP 认设备**;若同一 IP 上来一个不同 `device_id`,视为新设备并要求重新配对,而不是静默复用旧档案。
- 未配对设备只允许 `HELLO`/`GET_CAPS`/`GET_STATUS`,不允许发射 —— 与 `policy.tx_requires_token` 配合。

### 8.2 授权令牌(HMAC-SHA256 + 单调 `seq`,**无"有效期"概念**)在 App 侧怎么做

**以契约 §7.1 为唯一实现依据**(原文只写「App 计算的短时令牌」,v1.1 已补齐可实现的定义):

```
token      = HMAC-SHA256(key = pairing_secret,
                         msg = device_id | seq_be32 | f_hz_be64
                               | mod | power_dbm_x10_be16)
token_wire = base64url(token[0:16])          # 截断到 128bit
```

App 侧实现要点:

| # | 要点 | 理由 |
|---|---|---|
| 1 | 用 WebCrypto `crypto.subtle.importKey`/`sign` 做 HMAC,**不要**自己实现加密原语 | 手写加密代码是缺陷来源 |
| 2 | `seq` 是**每设备独立的单调递增计数器**,持久化在 IndexedDB;**每次发射前递增** | 契约 §7.1(3):设备要求 `seq` **严格大于**已接受的最大值,这是防重放的核心 |
| 3 | `device_id` / `f_hz` / `mod` / `power_dbm` 都必须参与签名,且与 `TX_REQUEST` 中的实际值**逐字段一致** | 契约 §7.1(2):令牌绑定到**那一次具体发射**,不能挪用于改频或加功率 |
| 4 | `power_dbm` 用 `×10` 后的整数(`be16`) | 契约 §7.1(2)明确;避免浮点表示差异导致两端算出的字节串不同 |
| 5 | 令牌**不落库、用完即弃**;`base64url` 无 padding | 减少泄露面;与固件侧解码约定一致 |
| 6 | **不要**用时间戳做窗口校验 | 契约 §7.1 的「为什么不用时间戳」已说明:设备无 RTC,基于时间戳的窗口要么形同虚设要么误拒。窗口由设备侧按 `uptime` 相对量判断 |
| 7 | 若 App 长期离线积压了发射请求,`uptime` 相对窗口可能已失效 → **必须重新走门禁③④**,不要复用旧令牌 | 契约 §7.1(3).1 |

### 8.3 DeepSeek API Key 的存放位置与「是否允许用户自带 Key」

- **位置:只在 App 侧**,设备侧永不接触(契约 §1、§7;ADR-0002)。设备即使被拆解也拿不到 Key。
- **允许用户自带 Key(推荐默认)**:用户在设置页粘贴自己的 Key,App 只存本地、不上传、不参与任何同步。UI 必须写明「Key 仅存于本机浏览器」。
- **诚实说明风险**:纯 Web 环境**没有安全的密钥存储**。IndexedDB 内容对同源脚本完全可见;任何 XSS、恶意第三方依赖或浏览器扩展都能读走。**浏览器也不是能「防止用户看到自己的 Key」的环境**。因此:
  - Key 若是**用户自己的**,风险 ≈ 「用户自己的 Key 在自己的设备上」,可接受;
  - 若未来要内置**厂商共享 Key**,PWA 方案**不可接受**(任何人打开 DevTools 即可提取),必须改为「App → 自建网关 → DeepSeek」的服务端代理;本设计阶段**不建议**引入共享 Key。
- 缓解手段(叠加):① 严格 CSP,禁止内联脚本与未知来源;② 依赖锁定 + 完整性校验;③ Key 输入框 `type=password`、不回显、不写日志;④ 提供「不启用 AI」模式(本地规则库,见 AI 文档 §5.3)。
- 补充(已实测):DeepSeek 的 `POST /chat/completions` 在预检中**回显 `Origin` 并允许 `authorization` 头**,所以浏览器**可以**直连,不必自建代理;但这不改变「Key 在前端」这一事实(见 AI 文档 §2.1)。

### 8.4 传输与隐私

- 设备链路:PWA 一律 `wss://`(§1.3);原生外壳可对自签证书做固定。
- 云端链路:**只上传特征 JSON**,不上传原始码流、不上传位置、不上传设备标识(见 AI 文档 §7)。
- 审计:每次 `TX_REQUEST`(含被拒的)在 App 侧写 append-only 镜像,字段与设备侧对齐(时间、频率、功率、结果),支持导出。⚠️ **功率必须记两个字段**:`power_dbm`(**请求值**)与 `granted_power_dbm`(**实际生效值**,取自 `TX_STATUS`,契约 §3.7)。只记一个会让事后无法判断"这次是否发生过夹取" —— 而 C-12 允许用户逐次调功率,这一判断变得必要。

---

## 9. 对协议契约的反馈(**只报告,不修改契约**)

> `protocol/schemas/README.md` §7 已系统登记了「schema 镜像过程中发现的文档缺口」;下表中标注 **(已在 schemas §7 登记)** 的项与它呼应,不重复展开。本节只列**从 App 集成视角**看会阻塞实现的部分。

| # | 严重度 | 对象 | 问题 | 建议 |
|---|---|---|---|---|
| 1 | ~~高~~ ⚠️ **部分关闭** | §7 | ✅ 契约 §9.1 已把它写成"**不是本契约能补齐的字段,而是载体选型的直接约束**",并说明 PWA 只能靠用户装 CA 或内网域名+受信 CA、证书固定只有原生能做。**但 §7 表格正文仍写"自签证书 + 指纹校验"**,未按载体分列 —— 保留为待办(低风险,因 §9.1 已给出正确口径)。原问题:`wss://` 的**信任模型在浏览器中不可实现**:浏览器没有证书指纹固定 API,§7 写的「自签证书 + 指纹校验」只有原生代码能做;而 `ws://` 又在规范上被 HTTPS 页面按混合内容拦截。§7 把两种载体的信任方案写成了一个笼统的「指纹校验」 | 按载体分列:PWA 路径 = 用户安装设备 CA,或内网域名 + 受信 CA 签发证书;原生路径 = 证书固定 |
| 2 | ~~高~~ ✅ **已关闭(2026-09-21 定稿)** | §7.1(1) | ~~**Wi-Fi-only 的设备无法获得 `pairing_secret`**,因此纯 Wi-Fi 路径用户**永远无法发射**~~ | **契约 v1.1 §7.1(5) 已把该推论写成显式结论**,并给出处置选项(A 接受 / B 设备端本地授权 / C 带外传密钥 / **D USB 作为可信信道**,2026-09-21 新增),同时说明"不要用屏幕或标签传密钥"的理由(会破坏"在场约束")。App 侧按 **A+D** 处理:Wi-Fi-only 时发射页只读,并**把引导直接指向"插 USB 线"**(本批确实可用的路径,§3.1 路径 B)。**iOS 已放弃(C-06)**,该限制现在只影响"不插 USB 线"的降级场景。✅ **2026-09-21 用户选方案 A**:本批配对**只走 USB**,BLE 不承担配对职责 —— 因此原「A+D」收窄为「**D 单独成立**」,选项 B/C 均未采纳 |
| 3 | **高** | §3.8 | `SET_NET{ssid,pass}` **没有对应的结果回报**,App 无法知道配网成败(§3 路径 A 第 7 步只能靠轮询猜测) | 增加 `NET_STATUS{ok, ip, ssid, err}` |
| 4 | ~~中~~ ✅ **已关闭** | §3.7 | ✅ 契约 v1.1 §3.7 已定义完整的 `armed` 语义(两段式:第一阶段带完整参数+令牌 → `armed`;第二阶段**只带** `{armed_ack:true}` → `sent`;`CAPS.limits.arm_timeout_ms` 窗口内未确认则回落 `failed`/`arm_timeout`),并明确 **`armed` 期间射频必须零输出**。原问题:`TX_STATUS.state` 已列出 `armed`,但**未定义** `armed` 的触发条件、有效期、以及是否需要第二次 `TX_REQUEST` 才到 `sent` | 补充两段式发射的状态机,或删掉 `armed` 取值 |
| 5 | **中** | §7 / §3.8 | 审计写在设备侧环形日志,但**没有读取审计的消息类型**,App 无法做镜像与导出(A-05 的回执) | 增加 `GET_AUDIT` / `AUDIT_LOG` |
| 6 | ~~中~~ ✅ **已关闭** | §3.6 | ✅ 契约 v1.1 §3.6 **规则 5** 已定义「字段缺失时的降级规则(强制,两端同源)」,区分 `*_confidence` 有值 / `null` / **字段缺失**三种情况,并写明**缺失 = 未知,绝不默认当真**。原问题:新增字段是可选的,但契约没说缺失时怎么降级 |
| 7 | ~~中~~ ✅ **已关闭** | §3.5 / §3.7 | ✅ 契约 v1.1 §3.7 已规定**两方向复用同一 RLE 布局**,在 JSON 消息里以 **base64url 字符串**承载,并明确 **`REPLAY_SIGNAL` 不承载该结构**(按 `name` 引用设备本地信号,码流不经过 App) |
| 8 | **中(已推迟 v1.2)** | §3.8 | 契约 **§9.1 已显式登记**该推迟项(`STATUS` 字段名需先定硬件采样点与单位)。原问题:`STATUS` 载荷**未定义字段名**(只写「温度、供电、Wi-Fi RSSI、模块状态」)。§3 路径 A 第 7 步的配网探测**正是依赖它**,目前无法实现 | 补字段名 |
| 9 | ~~低~~ ✅ **已关闭** | §3.3 / §3.5 | ✅ 契约 v1.1 新增 **§3.6.1 时间基准**:`t_ms` = 设备**单调 `uptime_ms`**,**不是绝对时间**(设备无 RTC),跨重启不可比较;`sample_clock_hz` 是采样时钟频率而非时间戳。§3.3 的载荷行亦已注明 |
| ~~10~~ | ~~低~~ ✅ **已关闭(2026-09-21)** | §3.8 / ~~C-05~~ | ✅ **已关闭:不需要该能力**。原问题:无「设备侧离线记录 / 回读」能力,PWA 的前后台断连使"长时无人值守"无法实现。**用户已取消无人值守(`S1`)**(产品定义 §2.4)⇒ 该能力**没有需要它的场景**,已由契约 **§9 显式列为非目标**。原建议"若要支持需新增记录会话→回读消息对"**作废** | — |
| 11 | **低** | §3.2 / §4(已在 schemas §7 第 1 项登记) | `MODULES` 与 `CAPS.modules` 元素字段不一致:`driver_version`/`spi`/`ant` 只在 §3.2,`tx_allowed` 只在 §4。App 的模块页两个来源都要用 | 统一为一个元素定义 |
| 12 | **低** | §4 | `CAPS` 缺 UI 必需字段:设备**展示名**、**配对状态**(`bound:bool`) | 增加 `device_name`、`bound` |
| 13 | **低** | §3.1(已在 schemas §7 第 9 项登记) | `PONG.t_ms` 语义未明确(回显 `PING.t_ms` 还是设备本地时刻),影响 App 的 RTT 计算 | 明确为回显值 |
| 14 | ~~中~~ ✅ **已关闭(2026-09-21)** | §3.7 | ✅ 契约 §3.7 已新增 **`TX_STATUS.granted_power_dbm`**(可选;`armed`/`sent`/`failed` 时必须给出)。原问题:契约要求审计记"**实际生效功率**",但 `TX_STATUS` 的字段集合里**没有承载它的字段**,而 schema 是 `additionalProperties:false` ⇒ **固件照契约实现反而回报不了**。C-12(功率可调)使这一缺口变成实质阻塞:App 无法把"请求 20、实际 10"如实告诉用户 | 已按上述补充;schema `msg-tx_status` 同步 |

---

## 10. 与其他文档的关系

- 载体选型结论回答 **Q-04**(`docs/README.md` 待决问题汇总)。**已由用户决策收窄为"仅 Android + PWA"**(C-06),不再需要用户拍板。
- 平台事实不再在本文重复记录:**唯一记录地是** [`040-verified-platform-facts.md`](../00-overview/040-verified-platform-facts.md);本文的平台结论均引用其 F-2 / F-3 / F-5,待核实项引用 V-06。该文件的**复核触发条件**(6 个月、切换载体、DeepSeek 改名、增删模块、进入实现阶段前)同样适用于本文。
- AI 调用链、提示词、成本与评测见 [`../50-ai/010-deepseek-analysis.md`](../50-ai/010-deepseek-analysis.md)。
- AI 放 App 侧的决策与理由见 [`../adr/0002-ai-analysis-in-app.md`](../adr/0002-ai-analysis-in-app.md)(`accepted`)。
- 频段能力边界与覆盖空白见 [`../adr/0001-frequency-coverage-gap.md`](../adr/0001-frequency-coverage-gap.md)。
- 协议字段的权威来源只有一个:`../30-protocol/010-device-app-protocol.md`。
