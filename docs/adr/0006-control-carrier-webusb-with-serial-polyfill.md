# ADR-0006:USB 控制载体定为 **WebUSB 基线 + web-serial-polyfill**,而非 Web Serial 基线

状态:`accepted` · 日期:2026-09-21 · 决策者:用户(授权"选择最合适的方案") · 记录者:协调者

上游依据:
- [`0003-usb-wired-control-channel.md`](0003-usb-wired-control-channel.md)(USB 通道决策 —— 本 ADR 解决它的**载体**问题)
- [`0005-usb-only-batch-and-deferred-wireless.md`](0005-usb-only-batch-and-deferred-wireless.md)(本批只做 USB ⇒ USB 成为**唯一**通道,故载体选择从"体验问题"升为"单点问题")
- [`../00-overview/040-verified-platform-facts.md`](../00-overview/040-verified-platform-facts.md)(**F-7** —— 本 ADR 的全部事实依据)
- [`../40-app/010-mobile-app-design.md`](../40-app/010-mobile-app-design.md)(§1.2.1、§2.6 —— 受影响的设计面)

---

## 1. 背景与问题

`ADR-0005` 把 USB 定为**本批唯一控制通道与唯一配对路径**。但"用哪个 Web API 去访问这个 USB 设备"**从未被决策过**,原文档只停留在"caniuse 显示 Chrome for Android 支持 WebUSB,故理论可行"。

这留下一个真实的开放性:USB CDC-ACM 设备在浏览器里有**两个**入口 ——

| 入口 | 层次 | 特点 |
|---|---|---|
| **Web Serial**(`navigator.serial`) | 高层,**正是为串口设备设计的 API** | 端口选择器简单;`readable`/`writable` 流封装好 CDC 细节;**看不到"两个设备盲选"问题** |
| **WebUSB**(`navigator.usb`) | 低层,通用 USB | 覆盖面广;但需自己管理 `filters` / `claimInterface` / 端点传输,**过滤器写错会让用户在两个 USB 设备间盲选** |

直觉上应选 Web Serial(更简单、更贴合)。**本次核实证明这个直觉在本项目上是错的**,原因是 Android 的时间线 —— 见 §2。

本次任务:**核实事实并选定方案**(用户授权"你想办法核实一下 WebUSB 等信息,选择最合适的方案")。

---

## 2. 核实结论(证据见 F-7)

| # | 事实 | 来源 |
|---|---|---|
| 1 | **CDC 不在 WebUSB 的"受保护接口类"清单里**(清单只有 Audio/HID/MassStorage/Hub/SmartCard/Video/AV/Wireless)⇒ 规范**不禁止**网页声明 CDC 接口 | WebUSB 规范原文表 |
| 2 | 🔑 **Android 默认没有 CDC-ACM 驱动** —— Chrome 官方原文:*"一个值得注意的遗漏是 USB 转串行适配器通常实现的 USB CDC-ACM 类,因为 Android SDK 中没有用于与串行设备通信的 API"*;并明确*"Android 上可访问的设备更多,因为默认包含的驱动程序更少"* | Chrome for Developers |
| 3 | 🔑 **Android 的 Web Serial 是 Chrome 148(2026-04)才引入的**;WebUSB 则在 Chrome 61 级即长期可用 | Chrome 148 Beta 说明 |
| 4 | Google 官方 **`web-serial-polyfill`** 在 WebUSB 之上实现 Web Serial API,**README 点名 Android** 为其适用平台,且它就是 **Web Serial 规范的原型实现** | google/web-serial-polyfill |
| 5 | Android 上**已授权设备再次 `open()` 仍会弹系统授权框** | Chrome for Developers |
| 6 | Windows 上 USB 设备由系统串口驱动占用且需 WinUSB ⇒ **桌面 Windows 的 WebUSB 大概率不可用**,Web Serial 可用 | Chrome for Developers |

**关键推理**:事实 3 是决定性的。**"Android 支持 Web Serial"是一个 2026-04 才成立的、非常新的事实**,而本项目的载体选型已经完成。若把基线定在 Web Serial,等于:

> **把产品的最低系统要求钉死在 "Chrome 148+"** —— 而 WebUSB 路径不要求这么新的浏览器。

叠加事实 4:走 WebUSB **不需要放弃 Web Serial 的易用性**,因为 Google 已把 Web Serial 语义实现在 WebUSB 之上。⇒ **"WebUSB 覆盖面"与"Web Serial 易用性"不是二选一。**

---

## 3. 决策

**控制载体 = 一层传输抽象 + 两个后端,基线为 WebUSB。**

```
App 传输层(唯一对上层暴露的接口)
   │
   ├─ 后端 A(优先):原生 Web Serial —— navigator.serial
   │    探测:typeof navigator.serial !== 'undefined'
   │
   └─ 后端 B(基线,回退):WebUSB + web-serial-polyfill
        navigator.usb → polyfill 暴露与 A 相同的 API 形状
```

四条约束:

1. **基线是 WebUSB**(后端 B)—— 它是**必须能工作**的那条;后端 A 只是探测成功时的优化。
2. **两个后端必须同形** —— 正因 polyfill 实现的**就是 Web Serial API**,上层代码只写一次。**不得**为两个后端写两套业务逻辑。
3. **设备过滤器仍须精确 `VID/PID`** —— 这一点**不因载体决策而改变**,理由见 §4.1。
4. ⚠️ **不得把 Web Serial 写成硬依赖** —— 否则在 Chrome < 148 的 Android 上功能**整体不可用**,而那不是设备问题,是载体选择造成的自伤。

---

## 4. 为什么不是其它方案

### 4.1 为什么不干脆"只用 Web Serial"(最直觉的方案)

- **Android 上要求 Chrome 148+**(F-7-3),把最低系统要求人为抬高到 2026-04 之后;
- 且用户**没有升级浏览器的理由** —— 失败表现是"App 找不到设备",与设备故障**无法区分**。这正是本项目反复禁止的那类"静默失效"(同 §F-4d-4 的 WS2812 风险形态);
- ⇒ 否决为**基线**,但**保留为优先探测路径**(低风险优化,零代价)。

### 4.2 为什么不自研 WebUSB 上的 CDC 封装

不必自研 —— Google 的 polyfill **就是 Web Serial 规范的原型实现**,API 形状与标准一致。自研只会得到一个更差、更不标准、需要自己维护的等价物。

### 4.3 为什么不改用 CH343P 那条通路

- 每台产品多一颗桥芯片(量产 BOM 成本);
- 占用 UART0(GPIO43/44),**丢失 JTAG 调试**(ADR-0003 §3.2 的核心否定理由,未变);
- 它**同样**是 CDC-ACM,§2 的全部结论对它一样成立 —— **换通路不解决任何问题**。
- ⇒ 维持 ADR-0003 / ADR-0005 §2.4 的"走原生 USB"结论不变。

### 4.4 为什么不直接上原生 Android 壳(USB Host API)

原生壳是 **WebUSB 实测失败时的降级路径**(ADR-0003 §6),不是首选 —— 引入 Android SDK 工具链(F-5:本机未装),而本机目前**零新增工具链**即可完成 PWA 路径。**保持为降级路径,不在本 ADR 中启用。**

---

## 5. 后果

### 5.1 正面

| 后果 | 说明 |
|---|---|
| 最低系统要求不被人为抬高 | 不依赖 Chrome 148;WebUSB 覆盖面更广 |
| 不需要自研 USB 封装 | 复用 Google 官方 polyfill |
| 上层代码只写一次 | 两个后端同形(Web Serial API) |
| Android 上**恰好**是最有利的平台 | "Android 无 CDC 驱动"(F-7-2)——**本项目放弃 iOS 的决定在这里意外地再次受益** |

### 5.2 负面 / 必须承担

| 后果 | 处置 |
|---|---|
| 🔴 **仍需真机实测**(`claimInterface` 能否成功) | **V-17 / TC-074a**。官方文档给的是"预期可用",**不是**"本设备已验证"。**这是本批唯一阻塞级实测项**,失败即触发 ADR-0003 §6 降级路径 |
| ⚠️ **确切 PID 未知** | V-17 同一次实测取得;**在此之前不得把 PID 硬编码进代码** |
| ⚠️ **Windows 桌面自测会失败且易误判** | **V-18**:桌面自测走 Linux/macOS,或显式走 Web Serial。**开发文档必须写明**,否则会被误判为固件缺陷 |
| ⚠️ **polyfill 的边界即事实 2 的边界** | 若某台 Android 设备**恰好**内置了 CDC 驱动,后端 B 会失败 ⇒ 但这**不是静默失败**,`claimInterface` 会抛错;**错误必须原样上报给用户**,不得吞掉 |

### 5.3 对既有决策的影响

| 既有结论 | 是否改变 |
|---|---|
| ADR-0003:用原生 USB,**不用** CH343P | ❌ **不变**(§4.3) |
| ADR-0005:本批只做 USB | ❌ **不变**(本 ADR 是它的载体细化) |
| "WebUSB 过滤器必须用精确 VID/PID" | ❌ **不变**,且**更硬**:polyfill 同样受此约束 |
| 降级路径 = 原生 Android USB Host 小壳 | ❌ **不变**(ADR-0003 §6) |
| App 载体 = PWA | ✅ **加强**:现在 PWA 路径有官方文档支撑,不再只是"caniuse 说支持" |

---

## 6. 最终用户可见的后果

1. **首次连接会弹两次系统授权框**(`requestDevice` 后 + `open()` 时)∈ 这是 Android 的正常行为,**UI 必须预先说明**,否则用户会以为出错(F-7-2 事实 5)。
2. **Chrome < 148 的 Android 上走 polyfill 路径** —— 对用户**无差别**。
3. ⚠️ **不得向用户承诺"自动重连、无需交互"** —— Android 的授权框使这不可能。

---

## 7. 基线同步

| 目标 | 动作 |
|---|---|
| [`../00-overview/040-verified-platform-facts.md`](../00-overview/040-verified-platform-facts.md) | ✅ **新增 F-7**(全部证据);关闭 **V-14**;新增 **V-17 / V-18** |
| [`../40-app/010-mobile-app-design.md`](../40-app/010-mobile-app-design.md) §1.2.1 | ✅ 更新:把"未验证"改为"官方文档已确认 Android 预期可用 + 仍须实测",写入载体决策与 polyfill |
| [`../70-test/010-verification-plan.md`](../70-test/010-verification-plan.md) TC-074a | ✅ 更新断言:补 polyfill 路径与 Windows 注意项 |
| `docs/README.md` / `README.md` | ✅ 登记 ADR-0006 |
| **协议契约与 `protocol/schemas/`** | ✅ **不改动** —— 载体是 **App 侧**实现细节,而契约是**传输无关**的(§2.2 承载表只要求"字节流",不关心用哪个 Web API 取得字节流)。⇒ **schema hash pin 不变,无兼容性影响** |
| 固件文档 | ✅ **不改动** —— 设备侧仍是 CDC-ACM 从设备,对"主机用 WebUSB 还是 Web Serial 读它"无感知 |
