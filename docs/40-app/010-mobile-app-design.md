# 手机 App 设计(Wi-Fi / BLE 双通道)

状态: draft · 最后更新:2026-09-20 · 上游依据:

- `README.md`(项目定位、开发环境现状)
- `docs/README.md`(文档索引、阅读路线、**待决问题编号表**)
- `docs/00-overview/010-product-definition.md`(F-01..F-10、场景 S1–S4、成功判据 A-01..A-05)
- `docs/00-overview/040-verified-platform-facts.md`(**已核实平台事实 F-1..F-6,本文件的平台事实均引用它,不重复核实**)
- `docs/adr/0002-ai-analysis-in-app.md`(`accepted`:AI 分析放 App 侧)
- `docs/30-protocol/010-device-app-protocol.md`(`v1` + §7.1 v1.1 修订,**协议契约,权威来源,本文件不修改它**)
- `protocol/schemas/`(机器可读镜像,`v1.1`,36 个消息 schema + `common.schema.json`)

> 本文件是**设计文档**,不含任何实现代码。
> 平台事实的**唯一记录地是 `040-verified-platform-facts.md`**;本文只做引用与工程推论,不另起一套数字。
> 本文件对协议契约的意见**只报告不改动**,汇总见 §9。

---

## 0. 结论摘要

| 议题 | 结论 |
|---|---|
| 载体(回答 **Q-05**) | **推荐 PWA 作为第一载体**,但必须诚实承认两个平台缺口:**纯 PWA 在 iPhone 上不能用 BLE**(F-2),且**HTTPS 页面连 `ws://` 局域网地址在规范上被拦、需实测确认宽容度**(F-3 / V-06) |
| 对 iOS 的产品表述 | iOS = 「Wi-Fi/`wss` 模式可用,BLE 配网与近距 BLE 不可用」。S2 场景对 iOS 用户不可得 |
| 双通道 | 近距离优先 BLE,远距离/高吞吐走 Wi-Fi;扫频/捕获进行中**不切换**(§2.3) |
| 保活 | 心跳 PING/PONG 5s(契约 §3.1);**前后台切换后必须整页重载重建连接**,不能只重连 socket(§2.4) |
| 规模纪律 | 请求规模**必须**读 `CAPS.limits.ble_frame_max` 与 `max_edges`,不得硬编码 4096/3669(契约 §2.2.1、§3.4) |
| AI | 分析在 App 侧调用 DeepSeek,设备只回传 `features`(ADR-0002);链路细节见 [`../50-ai/010-deepseek-analysis.md`](../50-ai/010-deepseek-analysis.md) |

---

## 1. 载体选型对比(Q-05)

### 1.1 对比表

| 维度 | **PWA**(HTML/JS + Service Worker) | **Flutter** | **React Native / Expo** |
|---|---|---|---|
| BLE 能力(Android) | ✅ Web Bluetooth(`Bluetooth.requestDevice()`) | ✅ `flutter_blue_plus` 2.3.12 | ✅ `react-native-ble-plx` |
| BLE 能力(**iOS**) | ❌ **Safari on iOS/iPadOS 全版本不支持 `navigator.bluetooth`**;iOS 上所有浏览器都跑 WebKit(App Store 指南 2.5.6),故**换浏览器也不行** | ✅ 原生 CoreBluetooth | ✅ 支持 background mode on iOS |
| Wi-Fi WebSocket | ⚠️ 可用,但页面必须 HTTPS 且设备必须 `wss://`(§1.3) | ✅ 无混合内容限制,可直连 `ws://` | ✅ 同左 |
| 开发环境成本(本机 F-5) | **最低**:Node v22.22.1 + npm 已装,**零新工具链** | **高**:Flutter/Dart **未安装**(数 GB);Android 需 Android SDK;iOS 构建需 macOS + Xcode(本机无) | **高**:同左;Expo 需 `prebuild` 生成原生工程 |
| 后台保活 | ❌ 弱:切后台 socket 必断;iOS 上重连还可能卡死(§2.4) | ✅ iOS 需配 `Info.plist`,被唤醒约 10s 预算 | ✅ 需 `isBackgroundEnabled` + Android 前台服务 |
| 原生能力 | 低:**无证书固定 API**(§1.3)、蓝牙权限受浏览器管控、无后台 | 高:可做证书固定、前台服务、文件保存 | 高:同上 |
| 上架要求 | **无**(免安装、免审核);代价是进不了应用商店,且 iOS 依赖用户手动「添加到主屏幕」 | Google Play + App Store 审核(隐私清单、蓝牙权限说明) | 同左 |
| 迭代速度 | 服务端发版即生效,无需重新审核 | 需重新构建/发版 | 需重新构建/发版(Expo 可 OTA) |
| **推荐** | ✅ **主载体**:Android + 桌面浏览器 + iOS 的 Wi-Fi 模式 | ⭕ 备选:仅当必须覆盖 iPhone BLE 时 | ⭕ 备选:同左 |

### 1.2 已核实的平台事实(Web Bluetooth)

完整表格与来源见 [`040-verified-platform-facts.md` F-2](../00-overview/040-verified-platform-facts.md)(来源:[caniuse — Web Bluetooth](https://caniuse.com/web-bluetooth),2026-09-20 抓取)。要点:

- ✅ Chrome 桌面 56+、Edge 79+、Opera 43+、Chrome for Android、Samsung Internet 6.2+、UC 15.5+、Baidu 13.52+
- ❌ **Safari(macOS)全版本、Safari on iOS/iPadOS 全版本**、Firefox(桌面与 Android)、Opera Mini、KaiOS
- 全局支持率 78.02%
- caniuse 注明:第三方 Safari 扩展 **iOSWebBLE** 桥接 CoreBluetooth 在 JS 层 polyfill `navigator.bluetooth`,但属**用户态支持、非 WebKit 的一部分**,支持状态仍计为不支持。**不作为产品路径**,只登记为技术验证的临时手段。

工程推论 1:**iOS 上不存在「换个浏览器就行」这条路。** [App Store 审核指南 2.5.6](https://developer.apple.com/app-store/review/guidelines/) 要求「浏览网页的 App 必须使用相应的 WebKit 框架与 WebKit JavaScript」。iOS 上所有第三方浏览器都跑 WebKit,因此都不可能自带 Web Bluetooth。(欧盟/日本已有替代引擎的 entitlement,但那是**浏览器 App 的引擎许可**,不等于 Web Bluetooth 已被实现 —— **待确认**,不作为设计依据。)

工程推论 2:Web Bluetooth **只能在安全上下文(secure context)中使用**,受 `Permissions-Policy: bluetooth` 管控(默认 allowlist `self`),权限弹窗由 `requestDevice()` 触发且需要 [transient activation](https://developer.mozilla.org/en-US/docs/Glossary/Transient_activation)(用户手势)([MDN — Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API))。后果:**不能后台静默扫描**,每次连新设备都需要用户点击。

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

### 1.4 推荐结论,以及对 iOS 用户的诚实说明

**推荐:PWA 作为第一载体,按「Android 全功能 / iOS 仅 Wi-Fi 模式」分层交付。**

- **Android(Chrome/Edge/Samsung/UC/Baidu)**:PWA 可覆盖 F-07(Wi-Fi)+ F-08(BLE)+ 配网全流程;本机零新工具链(F-5);满足 A-01。
- **iOS**:PWA 只能覆盖 **F-07 的 Wi-Fi 通道**,前提是设备提供 `wss://` 且证书被 iOS 信任(§1.3)。**F-08 与 BLE 首次配网在 iOS 的纯 PWA 上不可实现** —— 这不是取舍,是平台缺失。
- **要让 iPhone 用户拿到 BLE**:只能加**原生外壳**(Flutter 或 RN/Expo),或接受「iOS 用 Wi-Fi 模式,BLE 不提供」。**Q-05 需要用户在此二选一上拍板。** 本文档的建议:**先交 PWA 验证协议与 UI,把原生外壳作为 iOS-BLE 的后续增量** —— 理由是协议的 Wi-Fi 路径与全部 UI 在两个载体上完全复用,而原生工具链在本机尚不存在。

---

## 2. 双通道连接管理

### 2.1 通道能力对照(契约 §2.2 / §2.2.1)

| 项 | Wi-Fi(WebSocket) | BLE(GATT) |
|---|---|---|
| 地址 | `wss://<host>:8266/v1`(PWA 必须 `wss`;原生可用 `ws`) | 自定义 Service + Notify/Write |
| 单帧上限 | `LEN` ≤ 65535 | **以 `CAPS.limits.ble_frame_max` 为准**(契约推得理论上界 3669B,出厂建议 3600B) |
| 吞吐 | 高(`SCAN_HIT`/`DATA_CHUNK` 受 §2.4 令牌桶限流,`stream_rate_hz` 默认 50) | 低,分片重组缺片 500ms 即弃整帧 |
| 角色 | 远距离、无人值守、长时扫频(S1) | 近距、免配网、调试(S2) |

**规模纪律(必须遵守,否则会吃到 `frame_too_large` / `edge_clamped`)**:

- 单次 `CAPTURE_START.max_edges` 不得超过 `CAPS.limits.max_edges`;超出会被**钳制**并回 `WARN{edge_clamped}`,`CAPTURE_DONE.truncated=true`(契约 §3.4)。App 应**先读 CAPS 再决定请求值**,把钳制当异常路径。
- BLE 路径上单帧 `LEN` 不得硬编码 4096 或 3669,一律读 `ble_frame_max`;超限设备回 `ERROR{frame_too_large}` 且**不静默截断**(契约 §2.2.1)。
- `DATA_CHUNK` 的分块上限是**承载相关**的,App 不得假设固定块大小(契约 §2.2.1)。

### 2.2 自动择优规则

App 启动与每次进入前台时**同时**探测两条通道,10s 内按序择优:

| 优先级 | 条件 | 选中 |
|---|---|---|
| 1 | 已绑定设备通过 BLE 在范围内且可连 | **BLE** |
| 2 | 已记住的地址可达且 `wss` 握手成功(≤5s) | **Wi-Fi** |
| 3 | 两者都不可达 | 进入「扫描设备」页,走 §3 配网/重连流程 |

补充:用户可在「设置」里把偏好固定为某一条(S1 远程部署时强制 Wi-Fi),固定后不再自动择优。**不要做「按 RSSI 猜距离」的判断** —— 设备不做功率标定(产品定义 §3.1 非目标),用信号强度推距离是伪精度。

### 2.3 切换时的状态保持(关键策略)

契约 §5 规则 3 规定「发起新任务会隐式中止进行中的任务,并发 `WARN{preempted}`」——**切通道 = 切连接 = 会打断正在跑的扫频/捕获**。策略:

| 设备状态 | 是否允许切换 | 行为 |
|---|---|---|
| `IDLE` / `READY` | ✅ 允许 | 静默切换;新通道重做 `HELLO`/`GET_CAPS`;App 侧展示状态沿用 |
| `MODULE_READY` | ✅ 允许 | 同上;切换后**重新 `SELECT_MODULE`** 恢复活动模块(模块选择是设备侧状态,新连接不保证保留) |
| **`SCAN` / `CAPTURING`** | ❌ **禁止自动切换** | 保持当前通道直到任务结束;UI 把「切换」置灰并提示「扫频中,已暂停通道切换」。用户强切则先 `STOP_SCAN`/`CAPTURE_ABORT`,**明确告知本次数据作废**,再切 |
| 断线(被动) | — | 不切;按 §2.4 重连**同一条**通道,避免两个变量同时变化 |

App 侧要维护「设备状态影子副本」,以 `CAPS` + 各 `*_STATE`/`SCAN_DONE` 为唯一更新源(契约 §1「能力协商优先」)。**不要假设切换后设备还记得之前的模块选择。**

### 2.4 心跳、断线与重连

- **心跳**:契约 §3.1,`PING{t_ms}` / `PONG{t_ms}`,间隔 **5s**;连续 **3 次**(15s)无 PONG 判为断线;RTT 同时用于链路质量指示。BLE 路径 15s 内无任何帧同样判为断线。
- **重连退避**:1s → 2s → 4s → 8s → 15s 封顶,无限重试但**每 5 次向用户报一次**。
- **前后台切换是最大的坑(已核实的证据)**:
  - **iOS**:切后台后既有 socket 以 **1006** 关闭(预期);但回到前台后 `new WebSocket()` **可能永久卡在 `readyState=0 (CONNECTING)`** —— 已作为 WebKit Bug 308073 记录(2026-02-17 报告),报告中说明该问题**在 Safari 标签页与「添加到主屏幕」的独立模式上均复现**,同设备的 Brave 正常([bugs.webkit.org #308073](https://bugs.webkit.org/show_bug.cgi?id=308073))。同一报告记录:HTTP `fetch` 仍正常,**只有 WebSocket 子系统坏了**;加 cache-buster、先发一次 fetch「唤醒」均无效。
    → **设计决策**:监听 `visibilitychange`;回到前台若发现 WS 不可用,**先做一次 HTTP 健康探测**,若 HTTP 通而 WS 不通,直接 `location.reload()` 整页重载重建渲染进程,而不是无限重连。
  - **Android Chrome**:切后台/进入 Doze 后 WebSocket 会被断开,社区长期存在此问题([StackOverflow: Chrome/Android kills websockets running in background](https://stackoverflow.com/questions/55582103);PWA 场景 1005 断开与事件序列缺口有一手复现记录:[openclaw#2672](https://github.com/openclaw/openclaw/issues/2672))。
  - Service Worker **不能**用于保活:它本身会被终止,不适合长期持有 socket。
  - iOS 16.4+ 的 Home Screen Web App 支持 **Screen Wake Lock API**([WebKit blog](https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/)),可在**扫频进行中**申请屏幕唤醒锁避免息屏断连 —— 这是 PWA 侧唯一现实的缓解手段,且**只在前台有效**。
- **对 C-05(无人值守)的诚实结论**:PWA **不适合**长时间无人值守的远程监控。S1 若要「挂几小时自动记录」,记录职责应由**设备侧**承担,App 回前台后拉取结果 —— 这需要契约补「离线记录/回读」能力(§9 第 10 条),当前**不存在**。

---

## 3. 连接与配对流程

### 3.1 首次配对与配网

契约 §7.1(v1.1)已给出**可实现的配对定义**,与原文「首次配对绑定的长期密钥」配套:

```
设备开机若未配对:生成 32B 随机 pairing_secret(硬件 RNG)
  → 存 NVS(不可读回)
  → 同时生成 6 位一次性配对码,显示在设备端
App   → HELLO{client_id, pair_code}
设备  → HELLO_ACK{device_id, pair_ok:true} + 经 BLE 加密链路单独下发 pairing_secret
```

**路径 A(推荐,Android / 原生载体):BLE 配对 + 配网**

1. 用户点「添加设备」→ 点**「扫描」**(Web Bluetooth 必须由用户手势触发,§1.2 推论 2)。
2. 浏览器弹出设备选择器;用户选中设备(设备名含 `RFLAB-xxxx`)。
3. App 建立 GATT 连接 → 读设备屏幕上的 **6 位一次性配对码** → 发 `HELLO{app_version, proto_min, proto_max, client_id, pair_code}`。
4. 收 `HELLO_ACK{fw_version, proto, device_id, chip, uptime_ms, pair_ok:true}`;App 经 BLE 加密链路接收 `pairing_secret` 并只存本地。配对码的作用是**防中间人**(BLE 配对若被截获,攻击者拿不到设备屏幕上的码)。
5. `GET_CAPS` 拿 `CAPS`,校验 `proto` 与 `proto_min/max` 有交集;无交集 → 提示升级并终止(契约 §8)。
6. 展示 Wi-Fi 表单 → 发 `SET_NET{ssid, pass}`。
7. **设备入网后目前不会主动回报结果** —— 契约 §3.8 只有 `SET_NET` 单向消息,**没有对应的 ack**。降级做法:发完后在 60s 内轮询 `GET_STATUS`,看 Wi-Fi RSSI 是否变为有效值(读字段名待定,见 §9 第 8 条),或直接对候选地址做 `wss` 握手探测。(已作为契约反馈登记,§9 第 4 条。)
8. 握手成功 → 写设备档案(`device_id` + `pairing_secret` + 上次地址 + 通道偏好)进 IndexedDB。
9. **重新配对需设备端物理按键确认**(契约 §7.1(1)),App 不得提供「远程一键重新配对」。

**路径 B(iOS 兜底):Wi-Fi AP 直连**

1. 用户按设备说明把设备切到 AP 模式(硬件按键/上电窗口)。
2. 在**系统设置**里连上设备 AP(浏览器无法代劳,此路径不涉及 `SET_NET`)。
3. App 里手输设备地址(或尝试 `192.168.4.1`)→ `wss` 握手。
4. 证书不受信任时,iOS 会直接失败且**无法在 App 内绕过** → 需要「用户安装设备 CA」的一次性引导页(§1.3 第 3 条)。
   **注意**:此路径下 `pairing_secret` **无法建立**(契约 §7.1 明确只经 BLE 加密链路下发一次,Wi-Fi 路径不下发)→ Wi-Fi-only 的 iOS 用户**不具备发射授权能力**(`tx_requires_token=true` 时 `TX_REQUEST` 必然 `tx_bad_token`)。届时应把发射页设计为**只读**,并在 UI 上说明原因,而不是让用户反复失败。**这是 iOS 路径上一个此前未被记录的功能缺口,已登记为契约反馈(§9 第 2 条)。**

### 3.2 之后的重连

记住设备后,App 每次启动:① 尝试 BLE 直连(仅 Android/原生)→ ② 否则对「上次地址 + mDNS 名」做 `wss` 探测 → ③ 都失败则提示「设备不在线」并允许手动改地址。**不重复走配对流程。**

---

## 4. UI 信息架构

### 4.1 页面清单

| 页面 | 主要元素 | 关键交互 |
|---|---|---|
| **① 设备连接 / 配网** | 设备卡片(展示名、`device_id`、通道徽章、RTT)、添加设备、忘记设备、证书状态、配对状态 | 点击连接;BLE 扫描需用户手势;证书不受信任时给引导;`pair_code` 输入框 |
| **② 模块管理(S4)** | 模块列表(`slot`/`chip`/`present`/`bands`/`tx_max_dbm`/`tx_allowed`/`driver_version`/`spi`/`ant`)、活动模块高亮 | 点选 → `SELECT_MODULE`;`present=false` 置灰;切模块前警告「会中止当前任务」(契约 §3.2)。**`spi.bus` 三模块相同是正常的**(单 SPI2 + 独立 CS),**只有 `cs` 冲突才是配置错误** —— 不得把「bus 相同」当异常报警(契约 §3.2) |
| **③ 扫频(F-01/F-02)** | 起止频率(受 `CAPS.bands` 夹取)、步进、驻留、`max_edges`、阈值滑杆、**频谱条 + 瀑布图**、命中列表、进度、**真实覆盖区间**、命中完整性提示 | 开始/停止;点命中项 → 捕获页;`WARN{band_clipped}` 时把未扫区间画灰;**`hits_dropped > 0` 必须提示「本次明细不完整」并给「取回全部命中」按钮**(`GET_SCAN_HITS` 分页) |
| **④ 捕获与波形(F-03/F-04)** | 波形(RLE 展开)、`edges`/`duration_ms`/`truncated` 徽章、**特征面板**(§3.6 全字段,**含 `*_confidence`、`raw_semantics`、`incomplete`**)、解码结论 | 捕获/中止;波形缩放;`truncated=true` 提示「已达 max_edges 上限」;`incomplete=true` 时必须**显眼标注「数据不完整」** |
| **⑤ AI 分析(F-09)** | 分析卡片:`signal_type`/`protocol_guess`/`confidence`/`threat_level`、`matched_protocols`、`recommended_actions`、`caveats`、「已发送内容」折叠区 | 点「分析」上传特征;可展开查看**实际发送的 JSON**;置信度低标黄 |
| **⑥ 信号库** | 已保存信号(名称、频点、调制、来源:本地/设备)、特征预览 | `SAVE_SIGNAL` / `LIST_SAVED` / `DELETE_SAVED` |
| **⑦ 发射(F-05/F-10)** | 目标参数、**四道门禁**校验结果、二次确认区、`TX_STATUS` 结果 | 见 §5(b);`tx_whitelist` 为空 → 整页只读并显示「本机未授权任何发射频段」;无 `pairing_secret`(iOS Wi-Fi 路径)→ 同样只读(§3.1 路径 B) |
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
  │                     │ 门禁③ 用户显式确认(下)
  │                     │  任一不过 → 本地即拒,**不发帧**
  │ 二次确认弹窗 ◄──────│  展示:频点/调制/功率/重复次数/内容摘要
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
| 无 `pairing_secret` 却尝试发射 | 「本机未完成安全配对,无法发射」 | 引导走 BLE 配对(§3.1 路径 A);iOS Wi-Fi 路径下说明该能力不可得 |
| DeepSeek 401 / 402 / 429 / 5xx | 分别「Key 无效」/「余额不足」/「请求过快」/「服务暂时不可用」 | 见 AI 文档 §4.4 |
| 回到前台后 WS 不通但 HTTP 通 | (不提示) | 直接 `location.reload()`(§2.4,iOS WebKit Bug 308073) |

---

## 8. 安全

### 8.1 设备绑定与防串台

- 绑定依据:`HELLO_ACK.device_id` + `pairing_secret`(契约 §7、§7.1)。一台设备只与一个 `client_id` 配对;重新配对需设备端物理按键确认。
- 防串台:App 在 UI 与所有本地记录里以 `device_id` 为主键,**不按 IP 认设备**;若同一 IP 上来一个不同 `device_id`,视为新设备并要求重新配对,而不是静默复用旧档案。
- 未配对设备只允许 `HELLO`/`GET_CAPS`/`GET_STATUS`,不允许发射 —— 与 `policy.tx_requires_token` 配合。

### 8.2 授权令牌(HMAC-SHA256,30s 有效)在 App 侧怎么做

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
- 审计:每次 `TX_REQUEST`(含被拒的)在 App 侧写 append-only 镜像,字段与设备侧对齐(时间、频率、功率、结果),支持导出。

---

## 9. 对协议契约的反馈(**只报告,不修改契约**)

> `protocol/schemas/README.md` §7 已系统登记了「schema 镜像过程中发现的文档缺口」;下表中标注 **(已在 schemas §7 登记)** 的项与它呼应,不重复展开。本节只列**从 App 集成视角**看会阻塞实现的部分。

| # | 严重度 | 对象 | 问题 | 建议 |
|---|---|---|---|---|
| 1 | **高** | §7 | `wss://` 的**信任模型在浏览器中不可实现**:浏览器没有证书指纹固定 API,§7 写的「自签证书 + 指纹校验」只有原生代码能做;而 `ws://` 又在规范上被 HTTPS 页面按混合内容拦截。§7 把两种载体的信任方案写成了一个笼统的「指纹校验」 | 按载体分列:PWA 路径 = 用户安装设备 CA,或内网域名 + 受信 CA 签发证书;原生路径 = 证书固定 |
| 2 | **高** | §7.1(1)+ §3.8 | **Wi-Fi-only 的设备无法获得 `pairing_secret`**(§7.1 明确只经 BLE 加密链路下发一次,Wi-Fi 不下发),因此 iOS(纯 Wi-Fi 路径)用户**永远无法发射**。这不是 bug 而是设计的结果,但契约没有把「Wi-Fi-only ⇒ 不可发射」写成显式结论 | 在 §7.1 明确写出该推论,并给出 Wi-Fi-only 用户的配对/授权替代方案(例如设备端按键触发的一次性令牌导出),或显式声明其不具备发射能力 |
| 3 | **高** | §3.8 | `SET_NET{ssid,pass}` **没有对应的结果回报**,App 无法知道配网成败(§3 路径 A 第 7 步只能靠轮询猜测) | 增加 `NET_STATUS{ok, ip, ssid, err}` |
| 4 | **中** | §3.7 | `TX_STATUS.state` 已列出 `armed`,但**未定义** `armed` 的触发条件、有效期、以及是否需要第二次 `TX_REQUEST` 才到 `sent` | 补充两段式发射的状态机,或删掉 `armed` 取值 |
| 5 | **中** | §7 / §3.8 | 审计写在设备侧环形日志,但**没有读取审计的消息类型**,App 无法做镜像与导出(A-05 的回执) | 增加 `GET_AUDIT` / `AUDIT_LOG` |
| 6 | **中** | §3.6(已在 schemas §7 第 14 项部分登记) | v1.1 新增的 `*_confidence` / `raw_semantics` / `incomplete` 在 schema 里是**可选**字段(为保证 v1 报文合法),但契约没有一处说明「App 在字段缺失时应当如何降级」。App 与 AI 提示词都需要这条规则 | 在 §3.6 增加一句:缺失 ≠ 高置信;App 应把缺失的置信度按「未知/低」处理(AI 文档 §3.1 已按此假设写提示词) |
| 7 | **中** | §3.5 / §3.7(已在 schemas §7 第 3 项登记) | `TX_REQUEST.payload_rle` 的**编码未定义**:§3.5 只定义了**设备→App** 方向的二进制 RLE 布局,App→设备方向是否同一布局未说明 | 明确两方向复用 §3.5 布局,或定义 App→设备 的独立布局 |
| 8 | **中** | §3.8(已在 schemas §7 第 5 项登记) | `STATUS` 载荷**未定义字段名**(只写「温度、供电、Wi-Fi RSSI、模块状态」)。§3 路径 A 第 7 步的配网探测**正是依赖它**,目前无法实现 | 补字段名 |
| 9 | **低** | §3.3 / §3.5 | `SCAN_HIT.t_ms`、`RSSI_SAMPLE.t_ms`、`DATA_CHUNK` 元数据的 `sample_clock_hz` 未说明**时间基准**(设备 uptime?绝对时间?) | 注明基准;若需绝对时间,在 `HELLO_ACK` 或新增字段提供一次对齐点 |
| 10 | **低** | §3.8 / C-05 | 无「设备侧离线记录 / 回读」能力,PWA 的前后台断连使 S1 长时无人值守无法实现(§2.4) | 若要支持需新增「记录会话 → 回读」消息对;否则应在 §9 非目标里显式写明 |
| 11 | **低** | §3.2 / §4(已在 schemas §7 第 1 项登记) | `MODULES` 与 `CAPS.modules` 元素字段不一致:`driver_version`/`spi`/`ant` 只在 §3.2,`tx_allowed` 只在 §4。App 的模块页两个来源都要用 | 统一为一个元素定义 |
| 12 | **低** | §4 | `CAPS` 缺 UI 必需字段:设备**展示名**、**配对状态**(`bound:bool`) | 增加 `device_name`、`bound` |
| 13 | **低** | §3.1(已在 schemas §7 第 9 项登记) | `PONG.t_ms` 语义未明确(回显 `PING.t_ms` 还是设备本地时刻),影响 App 的 RTT 计算 | 明确为回显值 |

---

## 10. 与其他文档的关系

- 载体选型结论回答 **Q-05**(`docs/README.md` 待决问题汇总);是否采纳属用户决策。
- 平台事实不再在本文重复记录:**唯一记录地是** [`040-verified-platform-facts.md`](../00-overview/040-verified-platform-facts.md);本文的平台结论均引用其 F-2 / F-3 / F-5,待核实项引用 V-06。该文件的**复核触发条件**(6 个月、切换载体、DeepSeek 改名、增删模块、进入实现阶段前)同样适用于本文。
- AI 调用链、提示词、成本与评测见 [`../50-ai/010-deepseek-analysis.md`](../50-ai/010-deepseek-analysis.md)。
- AI 放 App 侧的决策与理由见 [`../adr/0002-ai-analysis-in-app.md`](../adr/0002-ai-analysis-in-app.md)(`accepted`)。
- 频段能力边界与覆盖空白见 [`../adr/0001-frequency-coverage-gap.md`](../adr/0001-frequency-coverage-gap.md)。
- 协议字段的权威来源只有一个:`../30-protocol/010-device-app-protocol.md`。
