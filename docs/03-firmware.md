# 固件(ESP-IDF / ESP32-S3)

状态:当前实现 + 阶段 2–6 目标。固件版本 **0.2.0**,协议帧头 **VER=1**。

权威:消息字段以 [docs/04-protocol.md](04-protocol.md) 为准,引脚以 [docs/02-hardware.md](02-hardware.md) 为准,验证步骤以 [docs/06-test-plan.md](06-test-plan.md) 为准;本文不复制契约全文。

## 1. 现状

- 已落地:阶段 1 骨架(帧协议、HELLO/GET_CAPS/PING、配对、WS2812、RF_KILL + 2.4G 总闸安全默认)+ 三模块**不发射**的射频探测。
- 已验证:经 CH343P 烧录、启动横幅、`HELLO`/`GET_CAPS`/`PING` 在 **921600** 下往返(毫秒级 RTT)。
- 模块探测**仍未通过复测**,但**根因未定**:最后一次实测(2026-10-25 第 3 次)三槽均 `present=false`。⚠️ **2026-10-31 的固件审计已证明当时"故障在 GPIO13 之后的 MOSI 线"的结论不成立** —— 至少有 4 个固件缺陷会产生完全相同的读数:
  - **H-01**:CC1101 状态寄存器用 `0x80|addr` 读(M2 的 `partnum=0xFF` 是必然结果,任何接线都不可能通过);
  - **H-02**:EN 拉高后只等 2 ms 就碰 SPI,且 M1 的 SDN 未受控(POR 未完成时 M1/M3 读数同样全 `0xFF`);
  - **M-02**:M1 的 `cts=0xFF` 是常量伪造,悬空 MISO 也读 `0xFF`,不能作为"命令/响应通了"的证据;
  - **H-03**:GPIO7(2.4G 总闸)从未被驱动,门控模型与文档自相矛盾。

  本版已修复上述缺陷;**必须重新采集诊断(见 docs/06 §7.2)才能判定硬件是否也有问题**。
- `CAPS.modules[].present=false` 的语义是"**已探测但未识别**"——前提是探测真的执行过;若 SPI 初始化失败,横幅会打 `# probe ran=0`,此时 false 表示"**未探测**",不是"确认没装"。
- 未实现:芯片驱动、扫频、捕获、特征、发射、四道门禁、令牌校验、`GET_STATUS`(见 §9)。

## 2. 源码结构

`firmware/src/` 是唯一参与编译的源码目录(`src/CMakeLists.txt` 使用 `GLOB_RECURSE src/*.*`);`main/`、`components/`、`boards/` 只是空占位,不参与构建。组件化拆分留到阶段 2(见 §8)。

| 文件 | 职责 |
|---|---|
| `src/main.c` | 启动编排、GPIO7/GPIO8 安全默认、UART0 收发、启动横幅、消息分发(HELLO/GET_CAPS/PING) |
| `src/frame.c/.h` | 帧编解码与接收状态机;`MAGIC/VER/TYPE/SEQ/LEN(LE)` 6 字节头、无 CRC;`LEN` 上限 16384;提供 `rflab_rx_reset()`/`rflab_rx_in_progress()` 供字节间超时重同步 |
| `src/rf_spi.c/.h` | SPI2 总线(1 MHz、mode 0、软件片选)、片选/供电轨/POR 时序(EN≥20 ms、M1 SDN 脉冲)、线检、回环自检、引脚宏 |
| `src/rf_probe.c/.h` | 三模块探测(不发射)、CC1101 三点 RSSI、诊断数据收集 |
| `src/wire_diag.c/.h` | **仅 bring-up 诊断构建**编译进去的现场接线诊断(默认不调用,见 §7) |
| `src/pairing.c/.h` | NVS 中的 32 字节 `pairing_secret`、`client_id` 绑定、base64url 输出、内存态 `last_seq` |
| `src/led.c/.h` | GPIO48 WS2812 指示语(RMT TX,10 MHz 分辨率),KILL 态显式/立即写黑,写操作带互斥锁 |

构建配置:`platformio.ini`(目标板/16 MB 覆盖、版本宏、烧录/监视速率)、`sdkconfig.defaults`(控制台/日志关闭、16 MB Flash、FreeRTOS 1 kHz、主任务栈 8192)、根 `CMakeLists.txt`。

固件实际使用的引脚如下(与 [docs/02-hardware.md](02-hardware.md) 一致):

| 信号 | GPIO | 说明 |
|---|---|---|
| SPI2 SCK / MOSI / MISO | 12 / 13 / 14 | 三模块共享;MISO 线检与 13–14 回环自检涉及这些线 |
| 片选 M1 / M2 / M3 | 15 / 16 / 17 | 低有效,软件控制;初始化前预置高再切输出 |
| 供电轨 EN M1 / M2 / M3 | 4 / 5 / 6 | 高有效(设计为 LDO EN)。M3 的 EN 只给模块上电,**与该模块的 CE 无关** |
| 2.4G 总闸 | 7 | **输出,上电第一步即显式拉低**;本批恒 0(0 = 软件不允许 2.4G) |
| RF_KILL | 8 | 低有效,上电第一步即拉低 = 禁止发射;本批恒 0 |
| M1 SDN | 9 | **开漏输出**(`GPIO_MODE_OUTPUT_OD`)+ 外部 100 kΩ 上拉(上电默认 shutdown);`rf_spi_init()` 里**释放**(置 1,靠外部上拉为高)≥100 µs → 主动拉低(置 0)→ 等 ≥10 ms。🔴 开漏是安全要求:现场旧接线把 SDN 直接接 GND,推挽输出高会与 GND 短路;开漏在旧接线下来退化为高阻(无高脉冲、不损坏引脚) |
| UART0 TX / RX | 43 / 44 | CH343P ↔ 控制通道 |
| WS2812 | 48 | RMT TX,板载灯 |
| 配对按键 | 38 | 低有效、内部上拉,本阶段只配置不消费 |
| M1 IRQ | 18 | AS10 nIRQ,本阶段不配置 |

**M3 的 CE 没有 GPIO**:`CE = AND(GPIO7, GPIO8)` 的 AND 门输出直接接 AS01 pin3。`GPIO7=0` 或 `RF_KILL=0` ⇒ `CE=0` ⇒ nRF24 处于 power-down、PA 不工作。因此 M3 可以在 KILL 保持低、CE 低的安全状态下由 GPIO6 上电做 SPI 只读探测;**M3 若将来要收发,必须 `GPIO7=1` 且释放 RF_KILL**,而本批固件没有任何把这两根拉高的代码。

## 3. 控制通道

| 项 | 值 |
|---|---|
| 物理通路 | CH343P USB-UART 桥(VID:PID `1a86:55d3`)→ `/dev/ttyACM0` → ESP32-S3 **UART0** |
| 引脚 | TX=GPIO43,RX=GPIO44 |
| 参数 | **921600** 8N1,无硬件流控 |
| 控制台/日志 | 关闭:`CONFIG_ESP_CONSOLE_NONE`、`CONFIG_ESP_CONSOLE_SECONDARY_NONE`、`CONFIG_LOG_DEFAULT_LEVEL_NONE`;UART0 由协议独占 |
| 复位 | DTR→IO0、RTS→EN,主机打开串口即可能复位设备一次;App/脚本必须容忍 |

**收发走 ROM 路径,不安装 UART 驱动。** 2026-10-22 实测:`uart_driver_install()` 返回 0、`uart_write_bytes()` 返回字节数、`uart_wait_tx_done()` 也返回 0,但线上**一个字节都没有**,接收同样为空;ROM 输出始终正常。为避免根因未明时堵死产品:

- 发送(帧,二进制):`esp_rom_output_tx_one_char()` 逐字节**原样**发送,带互斥锁。
  - 🔴 **C-01**:`esp_rom_output_putc()` == `ets_write_char_uart`,它会把 `0x0A` 扩成 `0x0D 0x0A`、并把 `0x0D` 直接丢弃。帧头/载荷是二进制,用它会让约 1.5% 的应答帧静默损坏(SEQ=10/13 或 LEN 字节含 0x0A/0x0D 时**必然**损坏)。**禁止**在帧路径使用 putc。
  - 文本(启动横幅、`mark0`、wire_diag)仍用 `esp_rom_output_putc()`。
- 接收:`uart_ll_get_rxfifo_len()` / `uart_ll_read_rxfifo()` 轮询 UART0 FIFO;主循环每次最多取 256 B、有数据时 `vTaskDelay(1)` 让出 CPU。**限制**:这是纯轮询且硬件 RX FIFO 只有 128 B,主机在长应答期间持续发帧可能溢出丢字节(靠 MAGIC 重同步恢复);彻底解决需安装 UART 驱动/中断,前置条件是先查清 docs/03 §3 记录的驱动异常。
- 帧内字节超时:超过 **200 ms** 无新字节就 `rflab_rx_reset()`,避免截断帧把后续新帧当载荷吞掉(契约 §2 未定义超时,这是实现层兜底)。
- 仍用 `uart_param_config()` + `uart_set_pin()` 设定 921600 与 RX 路由(不安装驱动),否则轮询读不到数据。

**波特率与启动标记**:复位后 ROM bootloader 自己的几行输出是 **115200**;进入 App 后横幅与协议帧统一为 **921600**。用 921600 打开会先看到一小段乱码,属正常。启动期的 `[0]start … [5]banner sent` 标记**从 `transport_init()` 之后才开始打**,因此它们本身也是 921600 可读的(此前 `[0]/[rx-idle-*]` 在切速率前输出,监视器里是乱码,L-07)。

**横幅与协议通道卫生(M-04)**:横幅是**未成帧的 ASCII 文本**,其中含 `'R'`(0x52)= 帧 `MAGIC`,设备 ID 里也有 `R`。严格按契约 §2 实现的解析器会看到"假帧头"。正确做法是:**校验 VER,失配时把当前字节重新处理**(不能简单丢弃),`tools/bringup_test.py` 已这样实现。本版不改横幅文本,只把它作为已知事实记录(长期方案:横幅改到调试构建或新增"设备启动"消息,需改契约,本批不做)。

## 4. 启动流程与安全默认

`app_main()` 的执行顺序是安全设计的一部分,**不要重排**:

1. **GPIO7(2.4G 总闸)=0 且 GPIO8(RF_KILL)=0**(先写输出锁存再切输出方向,避免抖动窗口)。`M3 CE=AND(7,8)=0` ⇒ nRF24 power-down。这是第一步,早于任何可能阻塞的初始化;复位到该语句之间引脚为输入态,依赖载板外部下拉保持 KILL。
2. 创建发送互斥锁,`led_init()` 立即写黑;可选 WS2812 自检(编译开关 `RFLAB_LED_SELFTEST`,**默认关闭**);置 KILL 态并启动低优先级 LED 任务。
3. 由 Wi-Fi STA MAC 末两字节生成 `device_id`(如 `RFLAB-7AA8`)。
4. 先以普通 GPIO 读 UART0 RX(GPIO44)空闲电平(必须在 `uart_set_pin` 之前),再 `transport_init()` 配置 UART0;失败只置 FAULT,之后的文本标记才开始以 921600 输出。
5. `pairing_init()` 初始化 NVS 并读取绑定状态;失败置 FAULT。注意 `NVS_NOT_FOUND`(从未配对)与其它 NVS 错误已区分,后者不再被当成"未配对"。
6. `rf_wire_check()`:在 SPI 初始化**之前**测 MISO 空闲电平(内部下拉/上拉各一次)。
7. `rf_spi_init()`:三路 CS **预置高**再切输出 → 三路 EN **预置高**再切输出 → 等 **≥20 ms** → M1 SDN(**开漏**)释放 ≥100 µs → 拉低 → 等 **≥10 ms** → 初始化 SPI2(1 MHz)。这个顺序是 POR 要求,不要为了"启动快"缩小时延;SDN 的开漏模式是安全要求(见 §2 引脚表)。
8. `rf_probe_run()` 执行探测(不发射,见 §6);总线初始化失败置 FAULT 并跳过探测(横幅 `# probe ran=0`)。
9. 控制通道正常时发送启动横幅,然后进入轮询接收循环。若 `transport_init` 失败,主循环**不解引用 NULL** `s_uart`,只做无害空转 + FAULT 灯。

安全默认的边界:WS2812 只做指示、**不在安全链上**(停更会保持最后颜色,所以 KILL 必须显式/立即写黑);发射与否的唯一硬件证据是 GPIO7 + RF_KILL 经 AND 门后的 CE=0。SPI 初始化不拉高 GPIO7/GPIO8。LED 语义:KILL 常灭(本批唯一实际状态)、IDLE 慢闪、ARMED 快闪、TX 常亮、FAULT 三连急闪。⚠️ **本批完成 HELLO 后不会切 IDLE**:门控未释放,灯保持常灭,避免"灯在闪 = 可以发射"的误读。

## 5. 帧协议与消息分发

帧头固定 6 字节,载荷为 UTF-8 JSON:

```
0        1      2      3      4        5
+--------+------+------+------+--------+--------+---------
| MAGIC  | VER  | TYPE | SEQ  | LEN LE | PAYLOAD
| 0x52   | 0x01 | u8   | u8   | u16    | LEN bytes
```

- 帧头**没有 CRC**;MAGIC 只用于重新同步,完整性由承载层保证。
- 接收状态机逐字节喂入;`VER != 1` 回 `ERROR{version_mismatch}`,`LEN > 16384` 在读载荷前拒掉并回 `ERROR{frame_too_large}`;200 ms 字节间超时复位(§3)。
- 已实现:`HELLO(0x01)→HELLO_ACK(0x81)`、`GET_CAPS(0x02)→CAPS(0x82)`、`PING(0x03)→PONG(0x83)`、`ERROR(0x7F)`。
- **状态机(契约 §5 规则 1)**:完成 HELLO 之前,除 `HELLO` 外一切命令(PING/GET_CAPS/未知类型/二进制帧)一律回 `ERROR{not_ready}`;HELLO 之后才进入正常分发。
- `HELLO` 校验 `proto_min..proto_max` 是否包含 1,并**要求 `client_id` 为非空字符串且长度 ≤64** —— 这对**所有** HELLO 生效(契约 schema 把 `client_id` 列为 required,不只是配对请求)。缺失/空串回 `ERROR{unsupported,"HELLO 缺少 client_id"}`,**既不建立配对、也不置 `hello_done`**(M-06:旧实现会用空 client 建 NVS 绑定并下发 secret,之后设备永久 `already_bound`,而按键改绑尚未实现 ⇒ 一次畸形 HELLO 就能锁死设备)。
- 首次配对生成 32 字节随机 secret,仅在 USB 首次 `HELLO_ACK` 下发一次(base64url,43 字符);已有绑定且 `client_id` 不同则回 `pair_error:"already_bound"`;只有随机源失败才回 `rng_failed`,NVS 写入失败会记日志并按失败应答(不冒充 RNG 问题)。
- 二进制帧(`TYPE & 0x80`)本阶段未实现,回 `ERROR{unsupported}`;未知类型同样回 `unsupported`。
- ⚠️ `GET_STATUS(0x52)` 仍未实现,HELLO 之后会回 `unsupported`(已知缺口,见 §9)。

字段、错误码与配对语义的权威是 [docs/04-protocol.md](04-protocol.md) 与 `protocol/schemas/`;本文只描述分发入口,不复述字段。

## 6. 射频探测实现(不发射)

目的只有一个:在没有驱动之前,先证明**接线 + 供电 + SPI 通路**可用。硬约束:**不写任何 TX FIFO、不发 CC1101 的 STX 等发射选通、不拉 CE、不碰 GPIO7/GPIO8**。顺序 M1→M2→M3,收尾做回环自检。严格说并非全只读:Si4463 需先 `POWER_UP`、CC1101 需 `SRES` 与调谐寄存器写入,但都**不涉及发射**。

**POR 时序(H-02,在 `rf_spi_init()` 里)**

- EN 拉高后等 **≥20 ms**(覆盖 Si446x `tPORH` 10 ms、nRF24 `T_POR` max 10.3 ms、CC1101 ramp ≤5 ms);
- M1 SDN(GPIO9,**开漏** + 外部 100 kΩ 上拉):释放(置 1,由上拉拉高)≥100 µs → 主动拉低(置 0)→ **等 ≥10 ms** 再发 POWER_UP/做 SPI 探测。开漏保证旧接线(SDN 直接接 GND)不会出现推挽高电平对地短路;旧板表现为"无高脉冲但保持低",新板得到正确 POR。
- Si446x POWER_UP 的 CTS 轮询窗口 **100 ms**(AN625 明确该命令可能更久;旧实现约 14 ms 不够)。

**通用诊断(只陈述事实,不做因果推断)**

- `rf_wire_check()`(SPI 初始化前):MISO 内部下拉读一次、上拉读一次。`pd=0,pu=1` = 悬空;`pd=1` = 被强拉高;`pu=0` = 被拉低。
- `line_check()`:**在对应 probe 之后**执行(旧实现放在 POWER_UP 之前,结论不可信)。拉低某槽 CS 后再做上拉/下拉两次读数:相等 ⇒ 选中后 MISO 被外部驱动(值即该电平);不等 ⇒ 读数随内部上下拉变,该时刻线上无外部驱动。**不能**据此断言 CS/MISO 没接:POR 未完成/未 POWER_UP 时 SDO 本来就是高阻。
- `line_sample()`:换一种 MISO 上下拉再发一笔**各槽位自己的只读命令**,比较读回是否变化。命令按槽位固定:`M1 = READ_CMD_BUFF(0x44)`、`M2 = 状态寄存器 PARTNUM(0xC0|0x30)`、`M3 = NOP(0xFF)`。🔴 **禁止再用 `0xB1`**(对 nRF24 是 `W_TX_PAYLOAD_NOACK` = 写 TX FIFO,对 CC1101 是未定义地址)。
- 读回随上下拉变 = 线上无人驱动;不随 = 读回被固定/有驱动。判读只写这个事实。

**M1 Si4463(命令与响应是两笔独立事务)**

1. 先发命令字节(POR 后先 `POWER_UP(0x02)`,参数 `01 00 01 C9 C3 80`;再发 `GET_PART_INFO(0x01)`),CS 拉高结束该笔事务。
2. 另起一笔:发 `READ_CMD_BUFF(0x44)`,返回流**第 2 字节**是 CTS(0xFF = 就绪);上限 **100 ms**。
3. CTS 就绪后继续读 8 字节:`CHIPREV, PART[15:8], PART[7:0], PBUILD, ID…`。**PART 为大端**,`part=(hi<<8)|lo`;`part==0x4463` 才置 `present=true`。
4. 横幅 `cts=` 显示**实际读到的 CTS 字节**(不再是常量 0xFF);响应全 `0xFF` 视为无响应(悬空 MISO 同样读 0xFF,不能当"通信成功")。

**M2 CC1101(状态寄存器必须用 0xC0 读头)**

- 先发 `SRES(0x30)` 软复位并等 5 ms;探测结束发 `SIDLE(0x36)` 回空闲。`cc1101_strobe()` 有**白名单**(只允许 SRES/SIDLE/SFRX/SRX),物理上不可能发出 STX 等发射选通。
- CSn 拉低后等 **200 µs** 再给 SCLK(手册 Table 22:不读 CHIP_RDYn 时最小 `tSP,pd=150 µs`);读事务的状态字节 bit7 为 `CHIP_RDYn`,必须为 0(晶振稳定)才采信数据。
- 🔴 **状态寄存器(0x30–0x3D)用 `0xC0|addr` 读**:`PARTNUM(0x30)`、`VERSION(0x31)`、`RSSI(0x34)`、`MARCSTATE(0x35)`。用 `0x80|addr` 会被当成命令选通(0xB0=SRES、0xB1=SFSTXON、0xB4=SRX、0xB5=STX),既读不到数据还可能触发发射选通;**配置寄存器(0x00–0x2E)仍用 `0x80|addr` 读**。
- ID:`PARTNUM==0x00` 且 `VERSION∈{0x14,0x04}` 才置 `present=true`。
- 仅当 `present` 时做 RSSI:在 **387 / 433.92 / 464 MHz** 调谐,`MCSM0=0x18`(IDLE→RX 自动校准)、`MCSM1=0x0C`(CCA_MODE=00、RXOFF_MODE=11 = 收完停在 RX),等 `MARCSTATE==0x0D`(RX,上限 50 ms),再等 2 ms 让 AGC 稳定,读 `RSSI(0x34)` 换算 dBm;每点结束回 IDLE。

**M3 nRF24L01**

- 读 `CONFIG(0x00)`,复位值 `0x08` 才置 `present=true`;同时读 `STATUS(0x07)` 供人工判读。
- 探测期间 `GPIO7=0` 且 `RF_KILL=0` ⇒ `CE=AND(7,8)=0` ⇒ power-down,**只读探测安全**;固件没有 CE GPIO(不要新增)。

**收尾自检**

- `rf_spi_deinit()` 先 `spi_bus_remove_device()` 再 `spi_bus_free()`,并把句柄置 NULL(L-02);然后把 GPIO13(MOSI)配成输出、GPIO14(MISO)配成带下拉的输入:现场用杜邦线短接 13–14,`hi=1 lo=0` 即 PASS,证明 ESP32 侧 SPI2 外设/GPIO 矩阵正常。**短接线插着时探测读数会被回环污染,复测前必须拔掉。**

**当前结果与根因结论(2026-10-31 审计修订)**

- 最后一次实测(2026-10-25):三槽均 `present=false`;M1 `cts=0xFF part=0xFFFF`、M2 `partnum=0xFF`、M3 `config=0xFF`。
- ⚠️ **上述读数不能作为接线故障的证据**:M2 的 `partnum=0xFF` 由 H-01 直接决定(即使 M2 硬件完美);M1 的 `cts=0xFF/part=0xFFFF` 可由 M-02(悬空 MISO 也是 0xFF)+ H-02(POR/SDN 时序)产生;M3 的 `config=0xFF` 可由 H-02(nRF24 POR 10.3 ms)产生。旧文档把故障定位在"GPIO13 之后的 MOSI 线 / 面包板接触 / 模块 pin 6"的结论**已被推翻**。
- 本版已修复 C-01/H-01/H-02/H-03/M-01/M-02/M-03/M-09/M-10/M-11 等。**下一步:按 docs/06 §7.2 重新采集诊断(拔掉 13–14 短接线),用横幅里的 `# probe …`、`# line …`、`# spi …`、`# gate …`、`# sdn …` 原始读数判定,再决定是否需要动硬件。**

## 7. 构建、烧录与自检

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware

pio run -e esp32s3                                        # 产物 .pio/build/esp32s3/firmware.bin
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0   # 经 CH343P 烧录
python3 ../tools/bringup_test.py --port /dev/ttyACM0      # 握手 + 能力 + RTT
```

- 工具链:PlatformIO(`espressif32` 55.03.311,pioarduino)+ **ESP-IDF 5.5.5**;`board=esp32-s3-devkitc-1`,板为 N16R8(16 MB Flash;8 MB 八线 PSRAM 本批不启用)。
- **16 MB 覆盖(L-01)**:板型 JSON 按 8 MB 定义,`platformio.ini` 用 `board_upload.flash_size = 16MB` / `board_upload.maximum_size = 16777216` 覆盖,构建**不再**出现 `Warning! Flash memory size mismatch detected. Expected 8MB, found 16MB!`;`Flash:` 上限显示为 16777216。
- `sdkconfig.esp32s3` 是生成物(已忽略)。改过 `sdkconfig.defaults` 或 `build_flags` 后先 `rm -f sdkconfig.esp32s3` 再编译,否则继续用旧配置。⚠️ `CONFIG_ESPTOOLPY_MONITOR_BAUD` 在 IDF 5.5 是无 prompt 的 int(控制台 NONE ⇒ 生成值恒 115200),**不**代表 serial monitor 速率;监视速率由 `platformio.ini` 的 `monitor_speed = 921600` 决定。
- 主任务栈:`CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`(M-05)。默认 3584 B 对 cJSON 递归解析不够,深嵌套 JSON 可导致栈溢出重启;本批只做提栈,限制 JSON 深度/专用解析任务是下一批项。
- **bring-up 诊断开关默认全部关闭(L-10)**:`platformio.ini` 的 `build_flags` 里**没有** `-DRFLAB_LED_SELFTEST` / `-DRFLAB_WIRE_DEBUG`。需要时手动加一行,**用完删掉**:
  - `-DRFLAB_LED_SELFTEST=1`:启动红→绿→蓝自检(TC-106 载板正品检查);
  - `-DRFLAB_WIRE_DEBUG=1`:启动时输出 `# dbg …` 接线诊断(会给 M1 发 POWER_UP、提前拉高三路 EN,**改变上电时序**,仅排障用;`wire_diag` 结束时恢复借用引脚)。
- 烧录/监视速率均为 921600;`upload_speed`/`monitor_speed` 已在 `platformio.ini` 配好。
- `bringup_test.py` 只用标准库:打开端口后按 DTR→IO0、RTS→EN 复位一次,等固件启动,打印横幅,再发 `HELLO` / `GET_CAPS` / `PING×3`;不发 `TX_REQUEST`,不碰 RF_KILL。`--raw` 逐帧打印,`--no-reset` 跳过复位。
- **自检脚本默认 `pairing:false`,不消耗唯一绑定**:只建立会话(pairing:false 也置 `hello_done`),不写 NVS;只有显式 `--pair`(且必须同时显式给 `--client-id`)才会配对并**永久绑定**设备。`rf_module_test.py` 同理。换绑仍需设备端 GPIO38 按键/擦 NVS(见 §9)。
- 自检 PASS 的判据:收到 `HELLO_ACK{fw=0.2.0, proto=1, device=RFLAB-…}`、`CAPS` 的三个槽位、3 次 `PONG`;`usb_frame_max` 必须为 16384。
- 若 `HELLO` 无应答:确认固件已烧录、设备已正常启动(921600 下应看到 `# RF-Lab`),并检查 `lsusb` 是否有 `1a86:55d3`。

## 8. 目标架构(阶段 2–6)

当前是单目录平铺。阶段 2 起按依赖单向拆组件,原则是**先有实现再建组件**,不预先造空目录:

- **阶段 2 驱动**:`rf_hal`(唯一允许直接调用 `esp_driver_spi/gpio` 的层)、`rf_types`、`rf_registry`、`rf_driver_si4463/cc1101/nrf24`。统一接口:`probe/init/set_freq/set_mod/get_rssi/tx_raw/rx_raw/enter_idle`;驱动只做寄存器与时序,不做频段/功率策略。出口:三模块识别 + 调谐误差 ±100 kHz。
- **阶段 3 捕获与特征**:`rf_capture` 用 RMT RX 记录边沿,按 `(dt_us, level)` RLE 输出,`max_edges=16384`;`rf_scan` 逐点调谐 + RSSI 形成扫频状态机;`features` 为纯函数、可 host 编译,猜测字段必须带置信度。出口:OOK 捕获→RLE→特征闭环。
- **阶段 4 发射门禁**:`rf_tx` 是唯一发射出口,依次过四道门禁(①频段白名单 ②功率上限 `min(请求, cap, tx_max)` ③HMAC 令牌 + `seq` 单调 ④模块在位复测),审计先写后发,最后合并 RF_KILL 与 2.4G 总闸(即 GPIO7/GPIO8 → AND → CE)。出口:四道门禁都能真实拒绝。
- **阶段 5 App / 阶段 6 验收**:PWA 经 CH343P 连接;验收 A-01…A-07,含拔线后 2 s 内回 idle 并停止发射。

任务级出口判据与零容忍用例见 [docs/06-test-plan.md](06-test-plan.md);跨组件依赖必须单向,`rf_driver_*` 之间不得互相引用。

## 9. 已知缺口

1. **`pairing_secret` 明文存 NVS**:NVS/flash 加密均未开启,能物理接触并读 flash 的人可拿到密钥。当前阶段接受,量产前必须处理。
2. **无 CRC / 无重传**:UART 位错不会被发现,只能靠上层 `PING` 探活或重发兜底;帧头按契约不加 CRC。实现层只加了 200 ms 字节间超时重同步。
3. **RF_KILL / 2.4G 总闸只能上电拉低,不能软件切换**:协议尚无该字段,App 既无法查询门控状态,也无法主动释放/重新置位;本批 LED 因此不会进入 IDLE/ARMED。
4. **配对恢复路径缺失**:secret 只在首次配对下发一次,若 `HELLO_ACK` 丢包则 App 拿不到;换绑需要 GPIO38 物理按键,而按键事件尚未消费。`last_accepted_seq` 也只在内存中,重启归零。本版至少堵住了"空 client_id 一次锁死"的路径(配对前校验非空)。
5. **`GET_STATUS(0x52)` 未实现**:契约 §3.8 已定义(无 ⏸ 标记),固件回 `unsupported`。属契约-实现缺口,下一批补实现或把 §3.8 标 ⏸。
6. **启动横幅是未成帧文本且含 `MAGIC` 字节**:见 §3(M-04),App/工具必须校验 VER 并回退重处理当前字节。
7. **cJSON 解析仍无深度限制**:本版只把主任务栈提到 8192 B;对端深嵌套 JSON 的理论风险仍在,下一批加深度预检或移入专用任务。
8. **`RFLAB_WIRE_DEBUG` 与正式探测不能混用**:wire_diag 会提前拉高 EN 并给 M1 发 POWER_UP,重复 POWER_UP 的运行期语义 AN625 未定义。
