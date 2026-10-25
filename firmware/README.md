# 固件(阶段 1 骨架 + 控制通道已迁移)

**状态(2026-10-21):阶段 1 骨架可烧录、可通信;控制通道 = CH343P → UART0。**

- 已实现:帧编解码(`frame.c`)、`HELLO/HELLO_ACK`、`GET_CAPS/CAPS`、`PING/PONG`、
  `RF_KILL` 上电默认禁止、WS2812 指示语、首次配对下发 `pairing_secret`。
- 未实现:射频模块驱动与探测、扫频、捕获、特征、发射、四道门禁、令牌校验。
  `CAPS.modules[].present` 一律 `false` = **未探测**,不是"确认没装"。

- 框架:**ESP-IDF 5.5.5**(PlatformIO,`espressif32` 55.03.311)· ESP32-S3 **N16R8**
  (板:VCC-GND YD-ESP32-S3,丝印 `HW678`)
- 架构设计:[`../docs/20-firmware/010-firmware-architecture.md`](../docs/20-firmware/010-firmware-architecture.md)
- 协议契约:[`../docs/30-protocol/010-device-app-protocol.md`](../docs/30-protocol/010-device-app-protocol.md)

## 0. 控制通道变更(2026-10-21)—— 这一节比其它都重要

| | 原生 USB Serial/JTAG | CH343P(USB-UART 桥) |
|---|---|---|
| 预期 VID:PID | `303a:xxxx` | **`1a86:55d3`** |
| 本机实测 | ❌ **从未枚举成功**:2026-10-18 单次尝试 9.4 min / 435 次 connect / 438 次 `error -71` / **0 次成功**;2026-10-21 换另一个 Type-C 口,150 s 内主机侧零事件 | ✅ 稳定枚举 → `/dev/ttyACM0`,烧录/日志/I/Q 都已验证 |
| 本批定位 | ❌ **不再是控制通道** | ✅ **唯一控制通道** |

**为什么改**:USB 是本批唯一链路,链路必须"已证实可用"。原生 USB 只有厂商规格与平台文档,
没有一次成功的枚举记录;CH343P 有连续可复现的实测。这不是偏好问题,是证据问题。

**代价(必须写在明处)**:

1. **日志与控制共用 UART0** ⇒ 本版把控制台/日志整体关闭,协议独占这条流。
   排障手段:① 复位后 **115200** 能看到 ROM bootloader 横幅;App 启动后的 `# RF-Lab` 横幅是 **921600**;② 需要完整 IDF 日志时另编一版。
2. **失去 JTAG 调试**(原生 USB 同时是 JTAG 口)。
3. **App 侧 WebUSB 过滤器要改**:`vendorId: 0x1a86, productId: 0x55d3`(原来是 `0x303a`)。
   ⚠️ CH343P 与原生 USB 都是 CDC-ACM —— 换的是**物理通路**,不是协议。
4. **DTR/RTS 接了自动复位**:主机打开串口或拉 DTR/RTS 会复位设备。这是 CH343P 板级走线,
   固件无法规避,App 侧必须能处理"打开端口后设备重启一次"。
   `tools/bringup_test.py` 已按此约定先复位再握手。

> 协议帧层(`frame.c`)与载体**完全解耦**,本次变更没有改它一行。
> 改的只有:`main.c` 的收发 I/O、`sdkconfig.defaults` 的控制台配置、`platformio.ini` 的注释。

## 1. 目录

```
firmware/
├── platformio.ini          构建目标
├── sdkconfig.defaults      有设计依据的配置项(每条都注明为什么不能随手改)
└── src/
    ├── main.c              入口:RF_KILL 安全默认 + UART0 帧收发 + §3.1 消息分发
    ├── frame.c/.h          帧编解码(契约 §2,与载体无关)
    ├── led.c/.h            板载 WS2812 指示语(硬件文档 §2.7,GPIO48)
    └── pairing.c/.h        长期密钥与配对状态(契约 §7.1(1),NVS)
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
| 射频模块探测 | ❌ | `CAPS.modules[].present` 一律 `false` |
| 扫频 / 捕获 / 发射 / 门禁 / 令牌校验 | ❌ | 阶段 2–4 |
| `RF_KILL` 的软件切换 | ❌ | **契约里还没有这个字段**(已知缺口) |
| `pair_code` / `bad_code` / `no_display` | ❌(本批无活路径) | 契约 §7.1(1) 明示本批不实现 |

## 3. 构建

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware && pio run -e esp32s3          # 产物:.pio/build/esp32s3/firmware.bin
```

> `sdkconfig.esp32s3` 是**生成物**(不入库)。改了 `sdkconfig.defaults` 之后必须删掉它再编译,
> 否则 IDF 会继续用旧的 `sdkconfig`(`rm firmware/sdkconfig.esp32s3`)。

## 4. 烧录与自检

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0   # 经 CH343P,实测可用
python3 ../tools/bringup_test.py --port /dev/ttyACM0      # 握手+能力+RTT,纯标准库
```

`bringup_test.py` 的行为:打开端口 → 按 CH343 约定复位一次(DTR→IO0,RTS→EN)→
等固件启动 → **打印启动横幅** → `HELLO` / `GET_CAPS` / `PING×3`。
全程不发 `TX_REQUEST`、不碰 `RF_KILL`。

**波特率**:App 横幅与协议帧**同为 921600**(2026-10-25 实测 ROM 输出路径也能跑 921600)。
⚠️ 复位后 ROM bootloader 自己那几行仍是 115200,用 921600 打开会看到一小段乱码 —— 正常。
横幅是 4 行以 `#` 开头的文本,只在启动时发一次;协议解析器只在 `MAGIC` 处开始,
所以会把它们当噪声跳过。⚠️ 这是**刻意的**:控制台已关闭,若再没有横幅,
设备出问题时就只剩"插上毫无反应"这一种现象,无法区分"没启动 / 启动失败 / 链路没起来"。

`--raw` 可逐帧打印;`--no-reset` 用于设备已在跑、不想让它重启的场合。

## 5. 本板 USB 的实测事实(为什么不是原生 USB)

| 口 | 实测枚举结果 | 结论 |
|---|---|---|
| 标注 **OTG** 的 Type-C | `1a86:55d3` → `/dev/ttyACM0`(cdc_acm) | **CH343P 桥**,通 UART0 |
| 另一个 Type-C | 2026-10-18:反复 `error -71` / `Device not responding to setup address`,**从未枚举**;<br>2026-10-21:插入后 150 s 内主机侧**零 USB 事件** | 原生 USB 通路,**当前不可用** |

判读依据(2026-10-18):两次都插在同一个主机口 `1-2`(内核日志确认),而 CH343P 那次
枚举成功 ⇒ **线材与主机口都是好的**,失败只与板子那个口有关。

⇒ 因此"固件能跑"与"App 能连上"必须分开看:前者已证实,**后者在本板只能经 CH343P 成立**。
丝印文字不能作为电气通路的证据,选口/选 VID:PID 一律以枚举结果为准。

## 6. 已知缺口(按重要性)

1. **`pairing_secret` 明文存 NVS**,且 NVS/flash 加密均未开启(见 `sdkconfig.defaults`)。
   影响:能物理接触设备并读取 flash 的人可拿到密钥。当前阶段接受,**量产前必须处理**。
2. **"只下发一次"没有恢复路径**:若首次配对的 `HELLO_ACK` 丢包,App 拿不到 secret,
   只能靠设备端物理按键重绑(按键消费尚未实现)。
3. **没有 CRC / 无重传**:UART 链路的位错不会被发现。协议帧头按契约不加 CRC,
   需要时由承载层或上层 `PING` 探活兜底。
4. **`RF_KILL` 只能上电拉低,不能软件切换**:契约尚无此字段(已知归属缺口)。

## 7. 宿主侧两项预防(仍有效)

| 问题 | 处理 |
|---|---|
| PlatformIO udev 规则缺失 ⇒ 非 root 打不开串口 | 已安装 `/etc/udev/rules.d/99-platformio-udev.rules` |
| **ModemManager 会主动打开 `/dev/ttyACM*` 发 AT 指令**,把字节注入帧流 | 已加 `/etc/udev/rules.d/99-rflab.rules`,按 VID `303a`/`1a86` 忽略 |

## 8. 射频探测联调记录(2026-10-25)

**条件**:用户接上 AS07-M1101D(CC1101)与 AS10-M4463D(Si4463),M3(nRF24)未接。
固件 `rf_probe_run()` 在启动时只读探测三块模块,并把结果打进横幅。

**实测输出**:

```
# wire MISO 14: pd=0 pu=1 -> floating(MISO 未接到任何驱动)
# probe M1 si4463   cts=0xFF part=0xFFFF present=0
# probe M2 cc1101   partnum=0xFF version=0xFF present=0
# probe M3 nrf24l01 config=0xFF status=0xFF present=0
```

**判读**:三块一致读 `0xFF`,且 MISO 用内部下拉读到 0、上拉读到 1 ⇒
**GPIO14 上没有接任何驱动源**。这不是协议/驱动问题,是**电气连接或供电问题**。

**为什么加"线检"**:三块同时 `0xFF` 有三种完全不同的原因,靠 ID 读数分不开 ——
① MISO 没接到模块 ② 模块没供电(输出高阻) ③ 有东西把线硬拉高。
`pd/pu` 两次读数能把它们区分开(见 `rf_spi.h` 的 `rf_wire_check`)。

**本次定位到的文档缺口**:AS10 模块第 2 脚 `SDN` **必须接 GND**(手册:"工作时为低电平")。
悬空 ⇒ 芯片 shutdown、MISO 高阻,读数正是全 `0xFF`。而本文件与接线文档此前**都没提这一脚**。
已补进 [`../hardware/wiring/README.md`](../hardware/wiring/README.md) §3.1(含两块模块的完整针序表)。

**复测方法**:改线后重新烧录(或复位),看横幅四行:

| 期望 | 含义 |
|---|---|
| `wire ... floating` 消失,改为 `driven-*` | MISO 上开始有驱动源 |
| `probe M1 si4463 cts=0xFF part=0x4463 present=1` | Si4463 通了 |
| `probe M2 cc1101 partnum=0x00 version=0x14 present=1` | CC1101 通了 |
| `# rssi ...` 三行 | CC1101 调谐 + 接收链路真的在工作 |

⚠️ 即便 `present=1`,也**只证明 SPI 读写通**,不证明射频性能(灵敏度/频偏/功率均未测)。

## 9. 第二次联调记录(2026-10-25 复测,用户重接 CC1101/Si4463 后)

**条件**:用户告知已按 [`../hardware/wiring/README.md`](../hardware/wiring/README.md) §3.1 重接
AS07-M1101D(CC1101,M2)与 AS10-M4463D(Si4463,M1);M3 未接。
固件:`0.2.0`,本次把 `transport_init()` 的波特率从 115200 改回 **921600**(见下),
并带上一版未提交的 `spi_up/spi_down` 自证行。烧录:`pio run -t upload --upload-port /dev/ttyACM0`,Hash verified。

**控制通道:✅ PASS(921600)**

```
HELLO_ACK: fw=0.2.0 proto=1 device=RFLAB-7AA8 chip=esp32s3
CAPS: 槽1 si4463 / 槽2 cc1101 / 槽3 nrf24l01p,三个 present 均 false
PING ×3: RTT = 1.7 / 1.9 / 3.5 ms
```

**模块探测:❌ 仍未通过(与 §8 相比,MISO 不再悬空,但读数仍不可信)**

```
# wire MISO 14: pd=0 pu=0 -> driven-LOW(被拉低)
# probe M1 si4463   cts=0xFF part=0xFFFF present=0
# line  M1 cs_pu=1 cs_pd=0 -> FLOATING(选中后仍无驱动:CS/MISO 没接或模块没上电)
# line  M2 cs_pu=0 cs_pd=0 -> driven-LOW
# spi   M1 pullup=0xFF pulldown=0x00 -> SPI采样正常(线上无人驱动)
# spi   M2 pullup=0x0F pulldown=0x8F -> 读回随上下拉变化
# probe M2 cc1101   partnum=0xFF version=0x34 present=0
```

**判读(证据 → 结论)**

1. **M1 完全没驱动 MISO**:CS 拉低、内部上拉时读 1、下拉时读 0 ⇒ 选中后仍高阻。
   ⇒ 要么 `NSEL` 没接到 GPIO15,要么 `SDN` 仍为高(shutdown,SPI 不可访问),
   要么 AS10 的 **GND(1 脚)** 没接好。
   用户已确认:**VCC 接 3.3V、SDN 接 GPIO18,且固件把 GPIO18 拉低**;
   另外按手册补 SDN 高→低 POR 时序后读数不变(见下表)⇒ SDN 时序基本可排除,
   **优先查 NSEL(pin 5)→ GPIO15 与 MISO(pin 7)→ GPIO14 这两根**。
2. **M2 的 MISO 有驱动但数据不可信**:选中的瞬间 MISO 被拉低(不是悬空),
   但读回值随 SPI 时钟/上下拉变化 —— `1MHz: 0xFF/0x34`、`100kHz: 0xE1/0x0F`、
   (固件里临时对调 SCK/MOSI 后)`0x00/0x00`。正常 ID 响应不会这样。
   ⇒ 更像 **共享 SPI 的 SCK/MOSI 没接对,或模块供电/地不良**,而不是帧协议解析问题。
   用户已确认 M2 的 VCC 也是 3.3V ⇒ 优先查 **SCK(pin 5)→ GPIO12、MOSI(pin 6)→ GPIO13、
   CSN(pin 4)→ GPIO16** 是否接在正确的一侧(模块 8 脚排针,不是 ESP32 排针位置)。
3. `wire ... driven-LOW` 是**在 `rf_spi_init()` 之前**(模块还没被 EN 上电)测的,
   说明上电前 GPIO14 被钳在低电平 —— 与"模块经 SPI 引脚寄生供电/地没共好"一致。

**本次做过的排除实验**(都未改变结论)

| 实验 | 结果 |
|---|---|
| SPI 降到 100kHz + 上电后等 100ms | M2 仍为 `0xE1/0x0F`,不是 `0x00/0x14` |
| 固件里临时对调 SCK/MOSI 引脚 | M2 变成恒 `0x00`,M1 仍全 `0xFF` ⇒ 不是"软件里反一下"就能救的接线错 |
| 按 Si446x 手册补 SDN 时序(高 1ms → 低,再等 20ms POR) | M1 仍 `cts=0xFF part=0xFFFF`;M2 读数从 `0x34` 变为 `0x08` ⇒ 时序不是 M1 无响应的原因(该实验代码未保留) |
| 同一固件连续复位 3 次 | 读数逐字节稳定 ⇒ 不是随机噪声 |

**需要的现场核对(按模块侧针脚,不要按 ESP32 排针位置数)**

| 模块 | 必须核对 |
|---|---|
| AS10(Si4463) | 1=GND、**2=SDN→GND**、5=NSEL→GPIO15、6=MOSI→GPIO13、7=MISO→GPIO14、8=SCK→GPIO12、**12=VCC→3.3V** |
| AS07(CC1101) | 1=GND、**2=VCC→3.3V**、4=CSN→GPIO16、5=SCK→GPIO12、6=MOSI→GPIO13、7=MISO→GPIO14 |
| 共享/电源 | 两块模块的 GND 必须与 ESP32 **共地**;三根 SPI 分别并联到两块模块的同名脚;VCC 只能 1.8–3.6V |

用万用表可一次判死:模块 VCC–GND 应为 **3.3V**,AS10 的 SDN–GND 应为 **0V**;
GPIO12/13/14/15/16 到对应模块脚应导通。

**顺带修复**:ROM 输出路径**并不限制** 115200 —— 本次把 `transport_init()` 改到 921600 后,
`bringup_test.py` 用默认参数即 PASS(RTT 从 ~6ms 降到 ~2ms)。此前 §0 与 `sdkconfig.defaults`
里"横幅只能 115200"的说法已更正。

