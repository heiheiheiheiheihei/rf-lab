# ADR-0003:用 USB 有线通道控制设备(并据此不采用 USB-OTG 外设)

状态:`accepted` · 日期:2026-09-20 · 决策者:用户 · 记录者:协调者

上游依据:
- [`../00-overview/010-product-definition.md`](../00-overview/010-product-definition.md)(F-11 / C-09 / C-10 / A-07 / Q-03 / Q-09)
- [`../10-hardware/010-system-architecture.md`](../10-hardware/010-system-architecture.md)(§2.3 引脚、§4 共存干扰)
- [ESP-IDF USB Serial/JTAG Controller Console](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html)
- [ESP-IDF USB OTG Console](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-otg-console.html)
- [caniuse: WebUSB](https://caniuse.com/webusb)

---

## 1. 背景与问题

产品在**屏蔽室**内使用,并已把 **2.4GHz 列入需求**(Q-03)。于是出现一个死结:

> M3 的 RFX2401C 以 **20dBm** 发射时,与主控 Wi-Fi/BLE 天线同处一个壳体、间距**厘米级**。20dBm 在几厘米处可达 mW/cm² 量级,而 2.4GHz 接收机阻塞门限约 −10~−20dBm ⇒ **PA 一发,本机 Wi-Fi/BLE 基本必然被压掉。**

后果不是"掉线"这么轻:它是**设备自己发出的发射动作,自己收不到中止指令**。用无线通道去控制一次 2.4GHz 发射,等于在最需要中止能力的那一刻失去中止能力。

同时在需求侧还有一项:**用户明确要求加入手机 OTG 连接控制功能,目的就是避免被 2.4GHz 模块干扰。**

---

## 2. 关键硬件事实(已核实)

ESP32-S3 有**两套完全独立的 USB 控制器**,但**共用同一对引脚**:

| | USB Serial/JTAG Controller | USB-OTG Controller |
|---|---|---|
| 实现 | **固定功能,完全硬件实现** | 可配置为多种 USB 设备类 |
| 提供的串口 | **CDC-ACM(直接就是串口)** | 需软件栈(TinyUSB/Device Stack)才能做 CDC |
| 引脚 | **GPIO20 = D+,GPIO19 = D−** | **GPIO20 = D+,GPIO19 = D−**(同一对) |
| PHY | 用**内部** PHY(默认) | 默认需**外部** PHY;要改用内部 PHY 须烧 `USB_PHY_SEL` eFuse |
| JTAG 调试 | ✅ 同时提供 | ❌ |
| 与 ROM USB console | 独立 | **不兼容**(ROM 的 CDC 栈与 TinyUSB 的描述符/栈不同) |
| 额外 BOM | **零** | 外接 PHY 需加芯片 |

引用要点(逐字):
- USB Serial/JTAG:"**fixed-function USB device that is implemented entirely in hardware**, meaning that it cannot be reconfigured to perform any function other than a serial port and JTAG debugging functionality."
- USB OTG:"By default, the USB_SERIAL_JTAG module is connected to the internal PHY of ESP32-S3, while the **USB OTG peripheral can be used only if the external USB PHY is connected**." 另:"**burning `USB_PHY_SEL` eFuse**" 可把内部 PHY 永久切给 OTG。
- 二者不可兼得:同一对 GPIO19/20 + 内部 PHY 归属唯一。

**两条结论直接从事实得出:**
1. **GPIO19/20 早已在该对引脚上**,所以 USB 控制通道**不消耗任何额外 GPIO**(引脚表此前已把它们列为 "USB D−/D+" 保留)。
2. **Serial/JTAG 与 OTG 互斥**。选一个。

---

## 3. 决策

### 3.1 采用 **USB Serial/JTAG 的 CDC-ACM** 作为有线控制通道

- 手机(USB 主机,经 OTG 线)↔ 设备(CDC-ACM 从设备)
- 设备侧**不写 USB 协议栈**:控制器是**硬件实现**的 CDC-ACM,**零 USB 栈代码、零额外芯片、零额外引脚**
- **保留 JTAG 调试能力**(OTG 方案会失去它),这对实现阶段价值很高
- 传输层对协议**完全透明**:CDC 是字节流,直接承载既有的 6 字节帧头 + PAYLOAD,**不新增任何消息类型**

### 3.2 **不采用** USB-OTG 外设

- 用 OTG 需:外接 PHY 芯片(**加 BOM**)或烧 `USB_PHY_SEL` eFuse(**不可逆**,且**失去 Serial/JTAG 与 JTAG 调试**),再加 TinyUSB 软件栈
- 而我们只需要"一条可靠的字节管道",**Serial/JTAG 已经就是那条管道**
- ⇒ 用复杂度与不可逆风险,去换取我们并不需要的"可配置为任意 USB 设备类"的能力,**不划算**

### 3.3 USB 作为**第三条等价传输**(不是"特殊模式")

Wi-Fi / BLE / USB **共用同一套帧格式与消息语义**(契约 §2)。要点:

- 同一时刻**只有一条活动控制链路**(避免双通道同时下发命令造成竞态);切换时须重新 `HELLO`
- USB 路径**天然满足**"操作者物理在场"这一安全前提(见 §5)
- USB 上**单帧无 BLE 那样的分片上限**,上限取 `CAPS.limits.usb_frame_max`(见契约);但仍受"捕获数据分块"约束,避免一次塞入过大帧

---

## 4. 这个决策解决了什么(以及没解决什么)

### 4.1 解决

| 问题 | 如何被解决 |
|---|---|
| **R-01:2.4GHz 发射压掉本机无线控制** | USB 是**有线**,射频干扰对其无影响 ⇒ 2.4GHz 满功率发射期间仍可控制与中止(判据 **A-07**) |
| **Q-09 附近的老问题:"Wi-Fi-only 设备无法发射"** | 若 USB 被采纳为可信信道(见 §5),绑定不再依赖 BLE ⇒ 不再存在"拿不到 `pairing_secret` 就永远不能发射"的死结 |
| 首次配对的可达性 | 插线即配对,**不依赖 BLE 可用性、不依赖 IP 发现** |

### 4.2 **没有**解决(必须写清,避免过度承诺)

- **自扰本身没有消失**:M3 发射仍然会压掉本机 Wi-Fi/BLE。USB 只是**绕过**它,**没有消除**它。故硬件侧的间距/极化/屏蔽措施与固件侧的时分策略**一条都不能省**。
- **手机仍可能无网**:屏蔽室内手机常无蜂窝/Wi-Fi ⇒ DeepSeek 不可用 ⇒ **离线规则库兜底必须真的可用**(F-09),不能只在文档里写一句"降级"。
- **WebUSB 不是零风险**:见 §6。

---

## 5. 安全含义

**USB 有线连接意味着"操作者物理在场"——这比 BLE 加密更强。**

- 攻击者要经 USB 配对,必须**物理接触设备**;而 BLE 可远程范围尝试
- 因此**建议**允许经 USB 下发 `pairing_secret`(即把 USB 列为可信配对信道),这会一次性关闭"无线不可用时无法绑定"的问题 —— 对应产品定义 **Q-09**
- ⚠️ **但有一条不可让步**:任何**人眼可读的载体**(OLED 屏、机身标签、LED/蜂鸣器编码)**绝不承载 `pairing_secret`**(见硬件文档 §2.5)。"人眼看屏/看标签"是**会被拍照、录屏、远程视频外泄**的信道;USB 是**物理端口**,两者性质不同,不能混为一谈。
  > **2026-09-21 补注**:该约束的**原载体 OLED 已延期到下一批**(C-07 ⏸),但**约束本身不随之失效** —— 它现在同时约束"如果改用机身标签或 LED 编码搬运 `pair_code`"这两个候选方案(产品定义 §8)。用它们搬运**码**可以,搬运**密钥**不行。
- 配对仍须保留:**一次性 `pair_code` + 设备端物理按键确认**。USB 在场性**替代不了**"重新绑定必须按键"(防的是"别人趁你不在插线改绑")
  > **2026-09-21 待办**:`pair_code` 在无屏条件下**没有搬运通道**(NQ-01)。**建议在 USB 链路上免校验 `pair_code`** —— 理由:USB 连接行为本身已证明在场,且不经射频,原设计要防的"BLE 被中间人截获"在 USB 上不成立。**但这是本 ADR 的建议,须用户确认(见契约 §7.1(1) 与产品定义 §8)**;在确认之前,BLE 链路**不得**静默降级为免码配对。

---

## 6. 风险与未决

| ID | 风险/未决 | 严重度 | 处置 |
|---|---|---|---|
| **USB-R1** | **PWA 能否经 WebUSB 访问 CDC-ACM 设备** —— 这是整条路径的唯一技术未知。[caniuse](https://caniuse.com/webusb) 显示 WebUSB 全球 77.89%,**Chrome for Android 支持**(Chrome for Android 152 起标 ✅),但有社区报告称移动端对部分设备类存在限制;已有 Android 上用 WebUSB 跑 CH340 串口的实践项目 | **高** | **必须真机实测**,列为实现阶段第一个验证项。**降级路径**:若 WebUSB 不可用 → 改用原生 Android USB Host API(`android.hardware.usb`)的小壳,或改提供"设备作 USB 主机 + 手机作从设备"的反向方案(后者需要手机支持 USB 从设备模式,通常不可行) |
| **USB-R2** | Type-C OTG 线材与手机供电方向(设备是否需自供电) | 中 | 设备用电池/USB 5V 供电;明确"手机不负责为设备供电",避免手机过流保护 |
| **USB-R3** | 与 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` 共用同一端口:控制流与日志流是否会互相污染 | 中 | 需在固件层**分离**控制通道与日志输出(见固件文档);否则 App 会读到调试日志 |
| **USB-R4** | 深睡/轻睡时 USB 设备会从主机消失 | 低 | 本产品为常供电便携设备,**不进入 deep-sleep**;轻睡须禁用自动休眠(USB 连接期间) |
| **USB-R5** | USB 线缆同时是"天线"(长线可能引入耦合) | 低 | 屏蔽室内影响可忽略;布局上避开 RF 前端 |

---

## 7. 备选方案与否决理由

| 方案 | 否决/保留理由 |
|---|---|
| **A. Serial/JTAG CDC-ACM(选定)** | 零额外 BOM、零额外引脚、保留 JTAG、硬件实现最省事。**选定** |
| B. USB-OTG + TinyUSB(设备模式) | 需外接 PHY 或烧 eFuse,失去 JTAG,且与 ROM console 不兼容。**否决**——为不需要的灵活性付不可逆代价 |
| C. 设备作 USB 主机(Host),手机作从设备 | 手机作 USB 从设备(peripheral)在 Android 上通常不可行。**否决** |
| D. 加一颗 USB-UART 桥(如 CH340)接到 UART | 可实现,但**多一颗芯片 + 多占 UART 引脚**,而 Serial/JTAG 免费提供同等能力。**否决** |
| E. 不做有线通道,只靠时分(发射窗口间控制) | 时分能缓解但**不能保证**中止指令可送达,且会牺牲发射连续性。**保留为固件侧补充手段**,不作为唯一方案 |

---

## 8. 影响的下游文档

- **契约**:§2.2 传输映射新增 USB;`CAPS` 增 `transports` 与 `usb_frame_max`;§7.1 配对信道说明
- **固件**:新增 `transport_usb` 组件;处理 "控制流 vs 日志流"分离(USB-R3);任务模型
- **硬件**:§2.3 明确 GPIO19/20 归 Serial/JTAG 且**不可同时用于 OTG**;新增 USB 连接器与 ESD/限流
- **App**:载体选型新增 WebUSB;有线模式与无线模式的择优与切换;USB-R1 实测项
- **测试**:USB 通道用例 + 2.4GHz 期间的控制保底用例(A-07)

---

## 9. 复议触发条件

出现下列任一情况,应重新评估本决策:

1. **USB-R1 实测失败**(PWA 无法经 WebUSB 访问 CDC-ACM)——此时需在"原生小壳"与"接受无有线通道"之间重选
2. 需要设备充当 **USB 主机**(例如直连 U 盘导出捕获数据)——那会重新把 OTG 外设拉回选项
3. 需要把 USB 用作**大容量存储/DFU 之外的设备类**(如 HID 键盘注入)
4. 产品形态改为**不需要物理接触**(例如全密封灌胶)→ USB 端口不可达,§5 的"物理在场"论证失效
