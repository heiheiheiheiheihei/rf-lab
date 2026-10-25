# 固件(ESP-IDF / ESP32-S3)

状态:当前实现 + 阶段 2–6 目标。固件版本 **0.2.0**,协议帧头 **VER=1**。

权威:消息字段以 [docs/04-protocol.md](04-protocol.md) 为准,引脚以 [docs/02-hardware.md](02-hardware.md) 为准,验证步骤以 [docs/06-test-plan.md](06-test-plan.md) 为准;本文不复制契约全文。

## 1. 现状

- 已落地:阶段 1 骨架(帧协议、HELLO/GET_CAPS/PING、配对、WS2812、RF_KILL 安全默认)+ 三模块**不发射**的射频探测。
- 已验证:经 CH343P 烧录、启动横幅、`HELLO`/`GET_CAPS`/`PING` 在 **921600** 下往返(毫秒级 RTT)。
- 模块探测当前未通过:三槽均 `present=false`(M1 `part=0xFFFF`、M2/M3 读数无效),证据指向现场 **MOSI(GPIO13→模块 pin 6)** 接线而非固件。
- `CAPS.modules[].present=false` 的语义是"已探测但未识别",不是"未探测",也不是"确认没装"。
- 未实现:芯片驱动、扫频、捕获、特征、发射、四道门禁、令牌校验。

## 2. 源码结构

`firmware/src/` 是唯一参与编译的源码目录(`src/CMakeLists.txt` 使用 `GLOB_RECURSE src/*.*`);`main/`、`components/`、`boards/` 只是空占位,不参与构建。组件化拆分留到阶段 2(见 §8)。

| 文件 | 职责 |
|---|---|
| `src/main.c` | 启动编排、RF_KILL 安全默认、UART0 收发、启动横幅、消息分发(HELLO/GET_CAPS/PING) |
| `src/frame.c/.h` | 帧编解码与接收状态机;`MAGIC/VER/TYPE/SEQ/LEN(LE)` 6 字节头、无 CRC;`LEN` 上限 16384 |
| `src/rf_spi.c/.h` | SPI2 总线(1 MHz、mode 0、软件片选)、片选/供电轨、线检、回环自检、引脚宏 |
| `src/rf_probe.c/.h` | 三模块探测(不发射)、CC1101 三点 RSSI、诊断数据收集 |
| `src/pairing.c/.h` | NVS 中的 32 字节 `pairing_secret`、`client_id` 绑定、base64url 输出、内存态 `last_seq` |
| `src/led.c/.h` | GPIO48 WS2812 指示语(RMT TX,10 MHz 分辨率),KILL 态显式写黑 |

构建配置:`platformio.ini`(目标、版本宏、烧录/监视速率)、`sdkconfig.defaults`(控制台/日志关闭、16 MB Flash、FreeRTOS 1 kHz)、根 `CMakeLists.txt`。

固件实际使用的引脚如下(与 [docs/02-hardware.md](02-hardware.md) 一致):

| 信号 | GPIO | 说明 |
|---|---|---|
| SPI2 SCK / MOSI / MISO | 12 / 13 / 14 | 三模块共享;MISO 线检与 13–14 回环自检涉及这些线 |
| 片选 M1 / M2 / M3 | 15 / 16 / 17 | 低有效,软件控制 |
| 供电轨 EN M1 / M2 / M3 | 4 / 5 / 6 | 高有效(设计为 LDO EN) |
| RF_KILL | 8 | 低有效,上电即拉低 = 禁止发射 |
| 2.4G 总闸 | 7 | 属发射门控,本阶段不驱动 |
| UART0 TX / RX | 43 / 44 | CH343P ↔ 控制通道 |
| WS2812 | 48 | RMT TX,板载灯 |
| 配对按键 | 38 | 低有效、内部上拉,本阶段只配置不消费 |
| M1 IRQ | 18 | AS10 的 SDN 已改接 GND 后归还,本阶段不配置 |

## 3. 控制通道

| 项 | 值 |
|---|---|
| 物理通路 | CH343P USB-UART 桥(VID:PID `1a86:55d3`)→ `/dev/ttyACM0` → ESP32-S3 **UART0** |
| 引脚 | TX=GPIO43,RX=GPIO44 |
| 参数 | **921600** 8N1,无硬件流控 |
| 控制台/日志 | 关闭:`CONFIG_ESP_CONSOLE_NONE`、`CONFIG_ESP_CONSOLE_SECONDARY_NONE`、`CONFIG_LOG_DEFAULT_LEVEL_NONE`;UART0 由协议独占 |
| 复位 | DTR→IO0、RTS→EN,主机打开串口即可能复位设备一次;App/脚本必须容忍 |

**收发走 ROM 路径,不安装 UART 驱动。** 2026-10-22 实测:`uart_driver_install()` 返回 0、`uart_write_bytes()` 返回字节数、`uart_wait_tx_done()` 也返回 0,但线上**一个字节都没有**,接收同样为空;ROM 输出始终正常。为避免根因未明时堵死产品:

- 发送:`esp_rom_output_putc()`(逐字节,带互斥锁);
- 接收:`uart_ll_get_rxfifo_len()` / `uart_ll_read_rxfifo()` 轮询 UART0 FIFO;
- 仍用 `uart_param_config()` + `uart_set_pin()` 设定 921600 与 RX 路由(不安装驱动),否则轮询读不到数据。

**波特率注意**:复位后 ROM bootloader 自己的几行输出是 **115200**;进入 App 后横幅与协议帧统一为 **921600**。用 921600 打开会先看到一小段乱码,属正常。启动期另有 `[0]start … [5]banner sent` 等 ROM 标记,用于判断卡在哪一步。

## 4. 启动流程与安全默认

`app_main()` 的执行顺序是安全设计的一部分,**不要重排**:

1. **RF_KILL 先拉低**(GPIO8 输出 0)。这是第一步,早于任何可能阻塞的初始化;复位到该语句之间引脚为输入态,依赖载板外部下拉保持 KILL。
2. 创建发送互斥锁,`led_init()` 立即写黑,可选 WS2812 自检(编译开关 `RFLAB_LED_SELFTEST`,当前构建打开),置 KILL 态并启动低优先级 LED 任务。
3. 由 Wi-Fi STA MAC 末两字节生成 `device_id`(如 `RFLAB-7AA8`)。
4. 诊断 UART0 RX 空闲电平,随后 `transport_init()` 配置 UART0;失败只置 FAULT,不阻断点灯与启动。
5. `pairing_init()` 初始化 NVS 并读取绑定状态;失败置 FAULT。
6. `rf_wire_check()`:在 SPI 初始化**之前**测 MISO 空闲电平(内部下拉/上拉各一次)。
7. `rf_spi_init()`:三路 CS 先拉高,供电轨 EN 拉高,再初始化 SPI2(1 MHz)。
8. `rf_probe_run()` 执行探测(不发射);总线初始化失败置 FAULT。
9. 控制通道正常时发送启动横幅,然后进入轮询接收循环(每次最多 256 字节,空转延时 2 ms)。

安全默认的边界:WS2812 只做指示、**不在安全链上**(停更会保持最后颜色,所以 KILL 必须显式写黑);发射与否的唯一硬件证据是 RF_KILL。SPI 初始化不碰 GPIO7/GPIO8。LED 语义:KILL 常灭、IDLE 慢闪、ARMED 快闪、TX 常亮、FAULT 三连急闪。

## 5. 帧协议与消息分发

帧头固定 6 字节,载荷为 UTF-8 JSON:

```
0        1      2      3      4        5
+--------+------+------+------+--------+--------+---------
| MAGIC  | VER  | TYPE | SEQ  | LEN LE | PAYLOAD
| 0x52   | 0x01 | u8   | u8   | u16    | LEN bytes
```

- 帧头**没有 CRC**;MAGIC 只用于重新同步,完整性由承载层保证。
- 接收状态机逐字节喂入;`VER != 1` 回 `ERROR{version_mismatch}`,`LEN > 16384` 在读载荷前拒掉并回 `ERROR{frame_too_large}`。
- 已实现:`HELLO(0x01)→HELLO_ACK(0x81)`、`GET_CAPS(0x02)→CAPS(0x82)`、`PING(0x03)→PONG(0x83)`、`ERROR(0x7F)`。
- `HELLO` 校验 `proto_min..proto_max` 是否包含 1;`GET_CAPS` 必须先完成 HELLO,否则回 `ERROR{not_ready}`。
- 首次配对生成 32 字节随机 secret,仅在 USB 首次 `HELLO_ACK` 下发一次(base64url,43 字符);已有绑定且 `client_id` 不同则回 `pair_error:"already_bound"`。
- 二进制帧(`TYPE & 0x80`)本阶段未实现,回 `ERROR{unsupported}`;未知类型同样回 `unsupported`。

字段、错误码与配对语义的权威是 [docs/04-protocol.md](04-protocol.md) 与 `protocol/schemas/`;本文只描述分发入口,不复述字段。

## 6. 射频探测实现(不发射)

目的只有一个:在没有驱动之前,先证明**接线 + 供电 + SPI 通路**可用。全程不写 TX FIFO、不拉 CE、不碰 RF_KILL/2.4G 总闸。顺序 M1→M2→M3,收尾做回环自检。严格说并非全只读:Si4463 需先 `POWER_UP`、CC1101 需 `SRES` 与调谐寄存器写入,但都**不涉及发射**。

**通用诊断**

- `rf_wire_check()`(SPI 初始化前):MISO 内部下拉读一次、上拉读一次。`pd=0,pu=1` = 悬空;`pd=1` = 被强拉高;`pu=0` = 被拉低。
- `line_check()`:拉低某槽 CS 后再做上拉/下拉两次读数。`cs_pu != cs_pd` 才说明"选中后 MISO 确实被驱动";相等 = 选中后仍悬空(CS/MISO 没接或模块没上电)。
- `line_sample()`:换一种 MISO 上下拉,再发一笔 `0xB1` 读事务,比较读回值,用于区分"模块没响应"与"SPI 采样坏了"。

**M1 Si4463(命令与响应是两笔独立事务)**

1. 先发命令字节(POR 后先 `POWER_UP(0x02)`,参数 `01 00 01 C9 C3 80`;再发 `GET_PART_INFO(0x01)`),CS 拉高结束该笔事务。
2. 另起一笔:发 `READ_CMD_BUFF(0x44)`,检查返回的 CTS 字节是否为 `0xFF`;最多 200 次、每次间隔 50 µs(约 10 ms 上限)。
3. CTS 就绪后继续读 8 字节:`CHIPREV, PART[15:8], PART[7:0], PBUILD, ID…`。**PART 为大端**,`part=(hi<<8)|lo`;CTS 就绪且 `part==0x4463` 才置 `present=true`。

**M2 CC1101**

- 先发 `SRES(0x30)` 软复位并等 5 ms。
- CSn 拉低后等 **200 µs** 再给 SCLK(手册 Table 22:不读 CHIP_RDYn 时最小 `tSP,pd=150 µs`);读事务的状态字节 bit7 为 `CHIP_RDYn`,必须为 0(晶振稳定)才采信数据。
- ID:`PARTNUM(0x30)==0x00` 且 `VERSION(0x31)∈{0x14,0x04}` 才置 `present=true`。
- 仅当 `present` 时做 RSSI:在 **387 / 433.92 / 464 MHz** 调谐,等 `MARCSTATE==0x0D`(RX,上限 50 ms),再等 2 ms 让 AGC 稳定,读 `RSSI(0x34)` 换算 dBm;每点结束回 IDLE。

**M3 nRF24L01**

- 读 `CONFIG(0x00)`,复位值 `0x08` 才置 `present=true`;同时读 `STATUS(0x07)` 供人工判读。

**收尾自检**

- `rf_spi_deinit()` 释放 SPI2,把 GPIO13(MOSI)配成输出、GPIO14(MISO)配成带下拉的输入:现场用杜邦线短接 13–14,`hi=1 lo=0` 即 PASS,证明 ESP32 侧 SPI2 外设/GPIO 矩阵正常。**短接线插着时探测读数会被回环污染,复测前必须拔掉。**

**当前结果**:三槽均 `present=false`;M1 `cts=0xFF part=0xFFFF`、M2 `partnum=0xFF`、M3 `config=0xFF`。MISO 在 SPI 初始化前即被拉低(不再是悬空),M1 选中后仍无驱动,而 13–14 回环 PASS ⇒ ESP32 侧正常,故障在 GPIO13 之后的 **MOSI 线 / 面包板接触 / 模块 pin 6**。AS10 的 SDN 已接 GND,GPIO18 归还为 IRQ(本阶段不配置)。

## 7. 构建、烧录与自检

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware

pio run -e esp32s3                                        # 产物 .pio/build/esp32s3/firmware.bin
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0   # 经 CH343P 烧录
python3 ../tools/bringup_test.py --port /dev/ttyACM0      # 握手 + 能力 + RTT
```

- 工具链:PlatformIO(`espressif32` 55.03.311,pioarduino)+ **ESP-IDF 5.5.5**;`board=esp32-s3-devkitc-1`,板为 N16R8(16 MB Flash;8 MB 八线 PSRAM 本批不启用)。
- `sdkconfig.esp32s3` 是生成物(已忽略)。改过 `sdkconfig.defaults` 后先 `rm -f sdkconfig.esp32s3` 再编译,否则继续用旧配置。
- 烧录/监视速率均为 921600;`upload_speed`/`monitor_speed` 已在 `platformio.ini` 配好。
- `bringup_test.py` 只用标准库:打开端口后按 DTR→IO0、RTS→EN 复位一次,等固件启动,打印横幅,再发 `HELLO` / `GET_CAPS` / `PING×3`;不发 `TX_REQUEST`,不碰 RF_KILL。`--raw` 逐帧打印,`--no-reset` 跳过复位。
- 自检 PASS 的判据:收到 `HELLO_ACK{fw=0.2.0, proto=1, device=RFLAB-…}`、`CAPS` 的三个槽位、3 次 `PONG`;`usb_frame_max` 必须为 16384。
- 若 `HELLO` 无应答:确认固件已烧录、设备已正常启动(921600 下应看到 `# RF-Lab`),并检查 `lsusb` 是否有 `1a86:55d3`。

## 8. 目标架构(阶段 2–6)

当前是单目录平铺。阶段 2 起按依赖单向拆组件,原则是**先有实现再建组件**,不预先造空目录:

- **阶段 2 驱动**:`rf_hal`(唯一允许直接调用 `esp_driver_spi/gpio` 的层)、`rf_types`、`rf_registry`、`rf_driver_si4463/cc1101/nrf24`。统一接口:`probe/init/set_freq/set_mod/get_rssi/tx_raw/rx_raw/enter_idle`;驱动只做寄存器与时序,不做频段/功率策略。出口:三模块识别 + 调谐误差 ±100 kHz。
- **阶段 3 捕获与特征**:`rf_capture` 用 RMT RX 记录边沿,按 `(dt_us, level)` RLE 输出,`max_edges=16384`;`rf_scan` 逐点调谐 + RSSI 形成扫频状态机;`features` 为纯函数、可 host 编译,猜测字段必须带置信度。出口:OOK 捕获→RLE→特征闭环。
- **阶段 4 发射门禁**:`rf_tx` 是唯一发射出口,依次过四道门禁(①频段白名单 ②功率上限 `min(请求, cap, tx_max)` ③HMAC 令牌 + `seq` 单调 ④模块在位复测),审计先写后发,最后合并 RF_KILL 与 2.4G 总闸。出口:四道门禁都能真实拒绝。
- **阶段 5 App / 阶段 6 验收**:PWA 经 CH343P 连接;验收 A-01…A-07,含拔线后 2 s 内回 idle 并停止发射。

任务级出口判据与零容忍用例见 [docs/06-test-plan.md](06-test-plan.md);跨组件依赖必须单向,`rf_driver_*` 之间不得互相引用。

## 9. 已知缺口

1. **`pairing_secret` 明文存 NVS**:NVS/flash 加密均未开启,能物理接触并读 flash 的人可拿到密钥。当前阶段接受,量产前必须处理。
2. **无 CRC / 无重传**:UART 位错不会被发现,只能靠上层 `PING` 探活或重发兜底;帧头按契约不加 CRC。
3. **RF_KILL 只能上电拉低,不能软件切换**:协议尚无该字段,App 既无法查询门控状态,也无法主动释放/重新置位。
4. **配对恢复路径缺失**:secret 只在首次配对下发一次,若 `HELLO_ACK` 丢包则 App 拿不到;换绑需要 GPIO38 物理按键,而按键事件尚未消费。`last_accepted_seq` 也只在内存中,重启归零。
