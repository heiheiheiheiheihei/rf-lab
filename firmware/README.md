# 固件(阶段 1 骨架)

**状态:编译通过(`pio run` = SUCCESS),但尚未在实机上烧录/验证 —— USB 链路当前枚举失败(见 §5)。**
未上机的代码不能被当作"已工作"。本文件的"已实现/未实现"两栏就是用来防这个的。

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

## 4. 烧录

```bash
pio run -e esp32s3 -t upload                       # 自动找口
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
pio device monitor                                  # 看启动日志(115200)
```

⚠️ **不要让 esptool 自动选口。** 本机在没有目标设备时会把主板自带的 `/dev/ttyS0`
当成上传口,报一个与真正原因无关的 "Failed to connect / No serial data received"。
排查时先看 `lsusb` 里有没有 `303a:xxxx`。

## 5. 当前阻塞:USB 链路枚举失败

宿主内核持续重试而**从未成功枚举过一次**:

```
usb 1-2: new full-speed USB device number 85 using xhci_hcd
usb 1-2: Device not responding to setup address.
usb 1-2: device not accepting address 85, error -71
usb usb1-port2: attempt power cycle
```

判读:**设备在电气上确实被检测到**(D+ 上拉 → 主机看到 full-speed 设备),
但**从不回应地址 0 的描述符请求**。这排除了两件事:

- ❌ 纯充电线 —— 那会**完全检测不到设备**
- ❌ 板上 `USB-OTG` 焊桥断开(V-19)—— 连接不通时同样**检测不到**

⇒ 剩下的方向是:线材/接触不良、供电不足、或芯片在枚举期间反复复位。
`old_scheme_first=1` 与延长 `initial_descriptor_timeout` 均无效(已回退),符合物理层判断。

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
