# 固件(阶段 1 骨架)

**状态:已上机跑通(2026-10-18)。** 经 CH343P 口烧录成功、`Hash of data verified`,
并从 UART0 抓到启动日志:

```
I (207) efuse_init: Chip rev: v0.2
I (292) main_task: Calling app_main()
# RF-Lab fw=0.1.0 proto=1 device=RFLAB-7AA8 boot
# RF_KILL=asserted(禁止发射)  modules: not probed
# ready
```

**但协议链路(原生 USB)尚未验证** —— 本板两个口的行为与预期不同,见 §5。
"能启动"与"App 能连上"是两件事,不能用一个的通过去代替另一个。

- 框架:**ESP-IDF 5.5.5**(PlatformIO,`espressif32` 55.03.311)·
  目标 ESP32-S3(**N16R8**,板 VCC-GND YD-ESP32-S3 / HW678)
- 架构设计:[`../docs/20-firmware/010-firmware-architecture.md`](../docs/20-firmware/010-firmware-architecture.md)
- 协议契约:[`../docs/30-protocol/010-device-app-protocol.md`](../docs/30-protocol/010-device-app-protocol.md)

## 1. 目录

```
firmware/
├── platformio.ini          构建目标
├── sdkconfig.defaults      有设计依据的配置项(每条都注明为什么不能随手改)
└── src/
    ├── main.c              入口:RF_KILL 安全默认 + USB 帧收发 + §3.1 消息分发
    ├── frame.c/.h          帧编解码(契约 §2)
    ├── led.c/.h            板载 WS2812 指示语(硬件文档 §2.7)
    └── pairing.c/.h        长期密钥与配对状态(契约 §7.1(1))
```

> 组件化(rf_hal / rf_driver_* 等)留到阶段 2 再拆。现在拆空目录只会在 `CMakeLists` 里
> 制造不会编译的空组件。

## 2. 本阶段实现了什么

| 项 | 状态 | 依据 |
|---|---|---|
| `RF_KILL` 上电即拉低(禁止发射) | ✅ | 硬件文档 §2.3(GPIO8,低有效) |
| 板载 WS2812 指示语(时序承载语义) | ✅ | 硬件文档 §2.7(GPIO48) |
| 帧编解码 `MAGIC/VER/TYPE/SEQ/LEN` | ✅ | 契约 §2(**帧头无 CRC**) |
| `HELLO` → `HELLO_ACK` | ✅ | 契约 §3.1 |
| `GET_CAPS` → `CAPS` | ✅ | 契约 §4 |
| `PING` → `PONG` | ✅ | 契约 §3.1 |
| 未知类型 → `ERROR{unsupported}` | ✅ | 契约 §6 |
| 首次配对下发 `pairing_secret`(base64url,仅 USB 一次) | ✅ | 契约 §7.1(1) |
| 射频模块探测 | ❌ | `CAPS.modules[].present` 一律 `false` —— **未探测**,不是"确认无模块" |
| 扫频 / 捕获 / 发射 / 门禁 / 令牌校验 | ❌ | 阶段 2–4 |
| `RF_KILL` 的软件切换 | ❌ | **契约里还没有这个字段**(已知归属缺口) |
| `pair_code` / `bad_code` / `no_display` | ❌(本批无活路径) | 契约 §7.1(1) 明示本批不实现 |

## 3. 构建

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware && pio run -e esp32s3          # 产物:.pio/build/esp32s3/firmware.bin
```

## 4. 烧录与看日志

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0   # 实测可用(经 CH343P)
pio device monitor --port /dev/ttyACM0                    # 115200,UART0 日志
```

**两个 USB 角色是分开的**(这是有意设计,见 `sdkconfig.defaults` 注释):

| 外设 | 用途 | 宿主侧 |
|---|---|---|
| UART0(GPIO43/44)← CH343P | **日志**(控制台) | `/dev/ttyACM0` |
| 原生 USB Serial/JTAG | **契约 §2 帧流**(给 App) | 期望 `303a:xxxx`,尚未验证 |

⚠️ 不要把两者塞进同一个外设:那样一旦固件/链路出问题,就**既拿不到日志也拿不到协议**,
现场完全瞎掉。本板正好经历过这个状态。
⚠️ 也**不要**让 esptool 自动选口 —— 它会挑主板自带的 `/dev/ttyS0`,报一个与真实原因无关的错。

## 5. 实测:本板两个 Type-C 口的真实身份(2026-10-18)

原来的假设是"标注 OTG 的口 = 原生 USB"。**实测推翻了它。**

| 口 | 实测枚举结果 | 结论 |
|---|---|---|
| 标注 **OTG** 的那个 | `1a86:55d3 QinHeng Electronics USB Single Serial` → `/dev/ttyACM0`(cdc_acm) | **CH343P 桥**,通 UART0。**不是**原生 USB |
| 另一个口 | 反复 `error -71`、`Device not responding to setup address`,**从未成功枚举** | 原生 USB 通路,状态未知(V-19 / V-20) |

关键反证:**两次都插在同一个主机口 `1-2`**(内核日志确认),而第二次(CH343P)枚举成功 ⇒
**线材与主机口都是好的**,失败只与板子那个口有关。

这条也是 F-4d-4 已登记风险的一个实例:**丝印文字不可作为电气通路的证据**。
接线与选口一律以枚举出的 `VID:PID` 为准。

> ⚠️ 因此下面两件事**必须分开看**:
> - "固件能跑" —— 已证实(§ 顶部日志)
> - "App 能连上" —— **未**证实:它要求原生 USB 枚举出 `303a:xxxx`。这正是
>   `V-17 / TC-074a` 的前置条件,也是 ADR-0003 / ADR-0006 的地基。

### 5.1 当时失败的日志形态

```
usb 1-2: new full-speed USB device number 85 using xhci_hcd
usb 1-2: Device not responding to setup address.
usb 1-2: device not accepting address 85, error -71
usb usb1-port2: attempt power cycle
```

判读:**设备在电气上确实被检测到**(D+ 上拉 → 主机看到 full-speed 设备),
但**从不回应地址 0 的描述符请求**。据此可排除:

- ❌ 纯充电线 —— 那会**完全检测不到设备**
- ❌ 板上 `USB-OTG` 焊桥**断开**(V-19)—— 连接不通时同样**检测不到**

⇒ 剩下:连接器虚焊/接触不良、供电不足、或芯片在该状态下未使能 USB PHY。

`old_scheme_first=1` 与延长 `initial_descriptor_timeout` 均无效(已回退内核参数),
符合"物理层"而非"枚举策略"的判断。

## 6. 上机后要跑的自检

```bash
python3 ../tools/bringup_test.py            # 握手 + 能力 + RTT,纯标准库
```

它按契约发 `HELLO(pairing:true)` / `GET_CAPS` / `PING×3`,并检查
`limits.usb_frame_max` 是否与固件 `RFLAB_USB_FRAME_MAX` 一致。

## 7. 宿主侧已做的两项预防

| 问题 | 处理 |
|---|---|
| PlatformIO udev 规则缺失 ⇒ 非 root 打不开串口 | 已安装 `/etc/udev/rules.d/99-platformio-udev.rules` |
| **ModemManager 会主动打开 `/dev/ttyACM*` 发 AT 指令**,把字节注入帧流(表现为偶发丢帧,极难排查) | 已加 `/etc/udev/rules.d/99-rflab.rules`,按 VID `303a`/`1a86` 忽略 |

## 8. 一条尚未验证的假设(阶段 0 门禁)

契约的 USB 承载依赖 **ESP32-S3 内建 USB Serial/JTAG 的 CDC**,而 App 侧要经
**WebUSB** 访问它。`V-17 / TC-074a` 要求真机确认:WebUSB 能否 `claimInterface`
以及确切的 `vendorId/productId`(预期 Espressif `0x303a`,PID 待实测)。
**本固件不改变这个前提**,但它一旦不成立,受影响的是 App 载体选择(ADR-0006),
不是固件本身。
