# 04 · 外部参考吸收:MYNOVA RFC V2

状态:**参考台账(非规格、非计划)** · 2026-11-24 · 上游 `Tomosawa/MYNOVA-RFC-V2@2ada17f`(2026-11-22)

> 本文只回答四件事:上游做过什么、哪些值得拿、怎么拿才不侵权、哪些别碰。
> **规则仍在 [`00-mvp.md`](00-mvp.md)(射频/命令)/ [`01-ui.md`](01-ui.md)(UI)/ [`02-plan.md`](02-plan.md)(计划)**;
> 本文与它们冲突时,以它们为准。上游代码**不入库**。

## 0. 结论

1. **能吸收的是「事实 + 接口 + 格式」,不是代码。** 上游 GPL-3.0,本仓库没有 LICENSE(默认保留所有权利);
   平台也不同(ESP32-S3 + Arduino + 2MB PSRAM vs C3 + ESP-IDF + 无 PSRAM)。整段移植既不合规也不划算。
2. **真正省时间的是 0.8.0 的 CC1101 部分。** 上游把「异步 OOK/2FSK + 4 档预设 + RMT 分段接收 +
   长间隔回放」踩过的坑写进了代码和测试;这些是 0.8.0 M3 最容易返工的地方,提前拿到约省 1–2 周的寄存器/时序调试。
3. **`.sub`/`.ir` 互操作是被低估的差异化。** 本仓库 0.7.x 只做自有 NVS
   ([`00-mvp.md`](00-mvp.md) §8),0.8.x 只读导入已定([ADR-003](adr/ADR-003-codebook-raw.md));
   上游已把语法、符号约定、多行 `RAW_Data`、多帧 `.ir` 全部跑通。
   这层适配放在存储/HTTP 边缘即可,**不碰射频核心**,却能把 Flipper 生态变成输入源。
4. **RAW 录制会倒逼一次存储决策。** 上游的演进方向是「NVS 只存元数据,脉冲进文件」
   (`Proto/SignalData.h:21-36`);本仓库没有文件系统、NVS 只有 24KB。**不要为了 RAW 直接改分区表**,
   0.8.0 建议「RAM 内录制 + 浏览器上传/下载」,把持久化留给有明确需求的版本。
5. **明确不吸收:** 54,762 行里的游戏/音频/对讲/TF 卡/3D 外壳、U8g2 页面框架、Arduino 服务层、
   无 SHA-256 的 OTA、把 Wi-Fi/MQTT 凭据写进备份。

## 1. 上游事实(可核)

| 维度 | MYNOVA RFC V2 | RF-Lab 0.7.2 | 对吸收的影响 |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N8R2,8MB Flash + 2MB PSRAM | ESP32-C3,4MB Flash,**无 PSRAM** | 内存策略不可移植 |
| 框架 | Arduino core 3.3.5 + U8g2/AsyncWebServer/ArduinoJson | ESP-IDF 5.5.x,纯 C,手写 HTTP | 驱动要按 IDF API 重写 |
| 射频 | CC1101 + 板载双频开关(310–350 / 390–434 MHz) | YS-RF34DL UART 固定码模块 | 不同层,互补而非替代 |
| 显示/按键 | 128×64 OLED + 12 键 + 页面栈 UI | 72×40,无本地按键 | UI 不可复用 |
| 存储 | app 2×3MB + LittleFS 1.8MB + TF 卡 | app 2×1.94MB + NVS 24KB,**无文件系统** | RAW 存储方案必须重做 |
| Web | Vue 3 + Vuetify 3 + Pinia,21 个 `.vue`;资源放 1.8MB LittleFS 分区,无 gzip、纯轮询 | 无框架单页,gzip ~30KB,SSE | 前端只借交互模式,不借栈 |
| HTTP | 44 条路由(41 路径),**零鉴权**,含固件上传 | 9 条路由,OTA 默认关闭 + 串口 token(0.7.3 规划 BOOT arm) | 反向教材,见 §11 |
| 体积 | **54,762 行 / 265 文件**(C/C++ 54,549 + `.ino` 213;游戏 7,674;GUI 5,347) | 固件 **5,806 行 C** | 体量差 10 倍,不能整包搬 |
| 主机测试 | 5 个 runner、约 10 个测试二进制、386 处 `assert`,**未进 CI** | C 测试 11,312 + TUI 12 + SAN(2026-11-24),CI 守门 | seam 设计可借,流程别学 |
| 许可 | GPL-3.0;多处注明移植自 Flipper(GPL-3) | **无 LICENSE** | 见 §2 |

上游自称:**CC1101 原始收发可用、6 种具名 OOK 协议 + 通用 PWM 解码、RAW 录制、红外学习、HA MQTT、Web OTA**;
这些是**上游的自述与代码事实**,本仓库**未编译、未上机复现**(见 §14)。

与本仓库路线图重合的只有三块:**0.8.0 CC1101**、**0.8.x 码库/RAW**、**0.9.0 MQTT/HA**。
NRF24L01 / SI4463 / LR1121、红外、音频、对讲在上游都不存在或不可移植,不要指望。

## 2. 许可:先说清楚,再谈复用

- 上游根目录是 **GPL-3.0**;但 264 个 C/C++ 源文件中有 **190 个**文件头写 `Copyright ... All rights reserved`,与 LICENSE 不一致。
  以 LICENSE 为准,但**不要拿这个仓库当「合规示范」**。
- 上游明确声明移植自 Flipper:`RFSubGhzPresets.h`(cc1101_configs)、`RFProtocol*.h`(princeton/came/nice/holtek)、
  `RFProtocolRegistry.h`(registry.c)、`FlipperFormat.*` / `FlipperSignalFile.*`。这些是 **GPL-3 的衍生文本**。
- 本仓库**没有 LICENSE 文件** → 默认「保留所有权利」→ **不能把上述文件拷进来**,哪怕只拷一段。

**结论:D1 清洁室重写(2026-11-24 已定)。** 只拿事实与接口;代码按 TI 数据手册/应用笔记、
Flipper 文件格式文档、HA MQTT 文档独立写。不把本仓库整体转 GPL,也不做「只读不落地」。

**可安全吸收的「事实」**:CC1101 寄存器/频点/数据率公式、PATABLE 与 FREND0 语义、MARCSTATE 等待、
TI DN013/DN022 的功率与带宽调整、`.sub`/`.ir` 的**键名/单位/符号约定**、HA Discovery 的**字段结构**、
各协议的**时序常数**(这些本身是测量值或芯片手册值)。

**不可吸收的「表达」**:文件组织、注释、状态机写法、测试代码、UI 布局。重写时不要「逐行翻译」——
逐行翻译仍是衍生作品。

## 3. 复用台账

处置口径:**采用**(照行为/参数重写)、**后置**(版本未到)、**跳过**(平台或范围不符)。

| # | 资产 | 上游位置 | 价值 | 处置 | 落点 |
|---|---|---|---|---|---|
| 1 | CC1101 异步 OOK/2FSK 收发骨架 | `Lib/CC1101Driver.cpp`(1,285 行) | 0.8.0 M3 核心,省掉寄存器摸索 | 采用(重写为 IDF) | 0.8.0 M3 |
| 2 | 4 档调制预设 + PATABLE/FREND0 | `Lib/RFSubGhzPresets.h` | 决定能否读 Flipper 录制 | 采用(按 TI 重建) | 0.8.0 |
| 3 | 脉冲模型 `int32 ±µs` + 长段拆分 | `CC1101Driver.cpp:854-896` | RAW 回放的正确表示 | 采用 | 0.8.0 |
| 4 | RMT 分段接收(2µs tick / 64ms idle) | `Lib/OokRmtCapture.*` | C3 同样可用,免写边沿拼装 ISR | 采用(C3 也支持 RX ping-pong) | 0.8.0 |
| 5 | 3 档灵敏度 + 宽带 DN022 调整 | `RadioHelper.cpp:188-260, 667-692` | 弱信号可用性 | 采用 | 0.8.0 |
| 6 | 协议解码注册表(7 协议/双帧/极性试探) | `Lib/RFProtocol*` | 0.8.x 才需要;首版不做 | 后置 | 0.8.x |
| 7 | `.sub` / `.ir` 语法与读写 | `Lib/FlipperSignalFile.*` | 生态互操作,替代「不做 .sub」 | 采用(按格式文档重写) | 0.8.x |
| 8 | RSSI 门控 RAW 录制 | `Lib/RawCaptureSession.*` | 录制体验;首版可简化 | 后置 | 0.8.x |
| 9 | 频率扫描状态机 | `Lib/FrequencyScanner.*` | 扫频/RSSI 曲线 | 后置 | 0.9+ |
| 10 | HA Discovery 实体模型 | `Services/HAManager.cpp` | 0.9.0 的机制可直接照 | 采用(改 topic/实体数) | 0.9.0 |
| 11 | 备份容器 `magic + 类型化记录 + CRC32` | `Services/BackupManager.cpp` | 若做备份,容器形状可用;**凭据必须排除** | 可选 | 1.0 |
| 12 | 打包脚本(分区断言 + sha256 + 整机镜像) | `scripts/package_firmware.py` | 补固件发布缺口 | 已采用 | 0.7.2 发版 |
| 13 | 主机测试 seam(SPI 寄存器模型 + 真实抓包) | `tests/native/**` | 驱动无硬件可测 | 采用设计 | 0.8.0 |
| 14 | UI 引擎/页面/动画/游戏 | `GUI/**`、`Pages/**` | 平台不符 | 跳过 | — |
| 15 | 多主页/快捷键/T9 | `DataStore` + `Pages/Home` | 产品灵感 | 可选 | 1.0 |
| 16 | TF 音频/录音/对讲/红外/3D 外壳 | `Audio`、`Talk`、`Ir`、`3DModels` | 不在本仓库范围 | 跳过 | — |
| 17 | 采集占用检测(`owned` 标志) | `Services/WebService.cpp` RF RX 段 | 多入口抢同一个射频前端 | 采用概念 | 0.8.0 |
| 18 | `/api/status` 单次聚合 + 重启后再连循环 | `WebService.cpp`、`DashboardView.vue` | 0.7.3 上传后等待重启 | 采用行为 | 0.7.3 |

**优先级:1 → 2 → 3/4 → 5 → 12/13 → 7 → 10。** 前五项是 0.8.0;8–11 是 0.8.x/0.9.0;17/18 是交互层小改动。

## 4. 0.8.0 CC1101:可复用的硬事实

> 以下寄存器值与常数来自上游代码与 TI 文档;本仓库**尚未在 C3 上验证**,只作为 ADR/驱动实现的输入。

### 4.1 分层(ADR-001 的输入)

接口草案已落在 [`adr/ADR-001-radio-hal.md`](adr/ADR-001-radio-hal.md);这里只记上游的分层事实。
上游把射频拆成五层,值得照抄的是**边界**而不是实现:

| 层 | 职责 | 对 `radio` HAL 的启示 |
|---|---|---|
| 驱动 | SPI、复位/选通、寄存器、PATABLE、发射播放 | `probe/init/reset`;不含业务 |
| 预设 | `(reg,val)` 表 + 名字映射 | `preset_id` 是能力,不是 UI 概念 |
| 采集 | RMT 分段 / GPIO 边沿 → 脉冲流 | 统一 `int32 ±µs` 出口 |
| 会话 | 录制/扫描/解码的生命周期 | 与传输层无关 |
| 编排 | 频段/天线、灵敏度、TX 路由 | 唯一知道「当前在收还是在发」的地方 |

本仓库的 `radio` HAL 建议最小面:`probe / init / rx_start(cfg) / rx_stop / tx_pulses(segments) /
tx_raw(int32*) / caps()`;`LONG/STOP` 对 CC1101 **不在 0.8.0 承诺**(能力矩阵已写死)。

### 4.2 调制预设(4 档)

| id | 显示 | `.sub` 里的 `Preset:` | 调制 | 关键寄存器 |
|---|---|---|---|---|
| 0 | OOK270k | `FuriHalSubGhzPresetOok270Async` | ASK/OOK,270kHz | `MDMCFG4=0x67`、`AGCCTRL0=0x40`、`AGCCTRL2=0x03`、`FREND0=0x11` |
| 1 | OOK650k | `FuriHalSubGhzPresetOok650Async` | OOK,650kHz | `MDMCFG4=0x17`、`AGCCTRL0=0x91`、`AGCCTRL2=0x07` |
| 2 | FM2.4k | `FuriHalSubGhzPreset2FSKDev238Async` | 2-FSK,dev 2.38k | `MDMCFG2=0x04`、`DEVIATN=0x04`、`FREND0=0x10` |
| 3 | FM47.6k | `FuriHalSubGhzPreset2FSKDev476Async` | 2-FSK,dev 47.6k | `DEVIATN=0x47`、`FREND0=0x10` |

**注意**:预设表**不含** `FREQ2/1/0`,也不含 `MCSM1`;加载后必须自己设频点并回读,
否则会继承上一次的频点。上游踩过一次转录错误:把 `MDMCFG0` 的值写到了 `0x0E`,而 `0x0E` 其实是 `FREQ1`,
频点被悄悄改掉;现在用静态 lint 断言「预设表永不写 `MDMCFG0`/`FREQ*`」。

### 4.3 接收路径

- `IOCFG2=IOCFG0=0x0D`(异步串行数据),`IOCFG1=0x2E`(Hi-Z);`PKTCTRL1=0x04`、`MCSM1=0x30`、
  `MCSM2=0x07`、`SYNC1/0=0`、`CHANNR=0`、`FSCTRL0=0`。
- 进入接收:`SIDLE → 1ms → SRX → 轮询 MARCSTATE(低 5 位)== 0x0D`,预算 25ms;失败必须回 IDLE。
- **写寄存器后回读校验**:上游对每个预设寄存器和 9 个必需寄存器逐一回读,不一致就判失败。
  这是「配置成功」与「芯片真的接受了」之间的唯一证据。
- **带宽跨 325kHz 要同时改 FIFOTHR/TEST2/TEST1/TEST0**(TI DN022);上游按
  `26e6/(8·(4+MDMCFG4[5:4])·2^MDMCFG4[7:6])` 算带宽再选表。
- RMT 参数:2µs/tick、最大段 64ms(`signal_range_max_ns=64,000,000`)、partial RX、
  用户块 = 硬件块一半(24 symbol)、16 深队列。`64ms` 是精心选的:64000/2=32000 tick,
  仍能塞进 15 位 duration。

### 4.4 发射路径

| 路径 | 用途 | 关键点 |
|---|---|---|
| 协议编码 | 已知协议(PT2262/CAME/…) | 帧间**不能**插 gap:帧头 Guard 就是帧间隔 |
| 段序列(RMT) | `.sub`/协议帧的 (level,duration) | 单段 >32767µs 必须拆成同电平原子段;奇数原子数要补一个低电平 |
| 原始流(GPTimer) | 任意 32 位时长、长间隔 | 1MHz 定时器 + ISR 播放,队列 2048;`maxLateUs` 可观测 |

- OOK 功率:`PATABLE[0]=0x00`(关载波),`PATABLE[1]=power`,`FREND0=0x11`。
  **单字节写 PATABLE 只会写 index 0** —— 上游因此出现过「发射弱 40dB」;
  OOK 必须写 `{0x00, power}` 两字节 burst。
- 2FSK:`PATABLE[0]=power`,`FREND0=0x10`。
- 功率表(433MHz,TI DN013):`{0x12,0x0D,0x1C,0x34,0x51,0x85,0xCB,0xC2}` → −30…+10dBm(8 档)。

### 4.5 灵敏度(3 档)

| 档 | AGCCTRL2 | AGCCTRL0 | MDMCFG4 | FIFOTHR | TEST2/1/0 |
|---|---|---|---|---|---|
| 0 标准 | 0x03 | 0x40 | 0x67(270k) | 0x47 | 0x81/0x35/0x09 |
| 1 高灵敏 | 0x07 | 0x40 | 0x67 | 0x47 | 0x81/0x35/0x09 |
| 2 最高 | 0x07 | 0x91 | 0x17(650k) | 0x07 | 0x88/0x31/0x09 |

上游自己留了一个不一致:档 1 的接收 profile 走 `AM650`(MDMCFG4=0x17),但 `applyRxSensitivity(1)`
写 `0x67`,**有效带宽取决于调用顺序**。重写时把「灵敏度」定义成一个完整寄存器组,不要拆成两个入口。

### 4.6 12 条最容易返工的坑

1. **CHIP_RDYn 超时不能发命令字节** —— 总线未就绪时写 `SRES` 可能把芯片锁死;超时就关事务、返回失败。
2. **状态寄存器读要带 burst 位**(`0xC0|addr`),单读位(`0x80`)读到的是别的寄存器。
3. **SPI 引脚归属**:Arduino 3.x 下 `digitalWrite` 前必须先 `SPI.end()`,否则静默失败;
   IDF 没有这层运行时断言,但同一根脚不能既挂 SPI 又当普通 GPIO——靠引脚表与启动自检兜住。
4. **PATABLE 两字节** —— 见 §4.4;写完回读。
5. **RMT 完成 ≠ 射频发出**:上游要求寄存器 + PATABLE 回读一致才算成功。
6. **奇数原子数**:RMT symbol 是两段一对;奇数要补低电平;连续重复帧之间用 1µs 而不是 1000µs,否则多出帧间隔。
7. **别给协议帧插 gap**:CAME/Nice 的帧头 Guard 就是帧间隔,插 gap 会让解码器认不出。
8. **Guard 不能靠时长猜**:9.6ms Guard 里夹 20µs 毛刺要先粘回去;只在「合法反电平段」提交。
9. **武装后的第一个边沿是等待,不是脉冲**;否则 5 秒静默会变成一条假帧。
10. **环形缓冲溢出 = 流不连续**:整段丢弃、重新锚定,绝不能把两端拼成一帧。
11. **partial RX 只在最后一块剥 idle 尾巴**;中间块结束在波形中间。
12. **单帧永不确认**:12 位残帧、重复 23 位包都被误判过;必须「等长 + 双帧一致」。

### 4.7 C3 移植差异(必须改)

| 项 | 上游(S3) | C3 上要改 |
|---|---|---|
| RMT | 48 word/通道、双块 ping-pong、用户块 24 symbol | **相同**:C3 也是 48 word 且支持 RX ping-pong,partial RX 可直接照搬 |
| 核 | 双核,ISR/解码在 core 1 | 单核:解码循环不能阻塞 Wi-Fi/BLE,优先级与 `vTaskDelay` 要实测 |
| PSRAM | 页面/大缓冲优先 PSRAM | 全部内部 RAM;`RFSignalDecoder` 光静态就 ~20KB,要砍双极性×7 解码器 |
| Flash | app 3MB×2 | 1.94MB×2,当前用 1.09MB → 新增驱动+采集+解码约 30–60KB,余量够但别把 1.94MB 用满 |
| RSSI 偏移 | 硬编码 74(433MHz) | 315MHz 会偏;**按频点标定**,不要照抄 |

### 4.8 码库与变更信号(ADR-003 的输入)

- 上游 NVS **每槽一个 key**(`rf_N`/`ir_N`),blob = `kind + nameLen + name + payload`;
  子结构自带 magic(`0xA5`)+ version,新字段**追加在尾部**,旧 blob 读出为 0。这与本仓库码库 v2
  的「新 key + 旧 key 保留 + 迁移标记」是同一问题的两种解;本仓库的 v1→v2 已上机验证(AC-22),
  **不需要照搬上游布局**,但「新增字段只追加、必带版本号」这条要遵守。
- 上游有两个 **revision 计数器**(码库/当前主页),每次写、删、恢复都 bump,是 MQTT 重发的唯一触发,
  也避免「定时全量重发」。本仓库事件环已有序号;0.9.0 可直接复用为「变更才发布」。
- 统一槽位编码 `0=空 / 1..100=RF / 101..200=IR` 只在「RF+IR 双域」有意义;本仓库单一射频域,
  **不要引入**这套偏移,保持码库 id 1..64。
- 名称上限 31 字节 UTF-8,按**字节**截断会切坏汉字码点——与 AC-23 的结论一致,保持拒绝而不是截断。

## 5. 存储:RAW 脉冲放哪里(关键判断)

上游自己的演进很说明问题:`RFSignalData` 早期把脉冲放 LittleFS 槽位,后来改成
**NVS 只留元数据 + 脉冲写 TF 卡 `.sub` 边车文件**(`Proto/SignalData.h:21-36`、`RawCaptureSession.*`)。
原因不是设计偏好,是容量:16,384 段 × 4B = **64KB/条**,NVS 放不下,也不该反复擦写。

本仓库现状:NVS 24KB(码库 64 槽 + Wi-Fi/BLE PIN),**没有文件系统**,双 OTA 槽各 1.94MB(用 1.09MB)。

| 方案 | 容量 | 代价 | 结论 |
|---|---|---|---|
| **A. RAM 内录制 + 浏览器上传/下载** | 4,096 段 ≈ 16KB/次 | 掉电即失;需 Web 文件读写 | **0.8.0 采用** |
| B. 新增 LittleFS 数据分区 | 0.75MB ≈ 11 条 64KB 录制 | 改分区表(需 USB 重烧一次)、改 `check_partitions.py`/`flash.sh`/文档 | 有持久化需求再做 |

**推荐 A 的理由**:本仓库是「实验室」不是「遥控器仓库」;一次录制→看清时序→发一次→导出 `.sub`,
比「存 100 条 RAW」更贴合使用场景。浏览器天然是文件系统,设备只做解析/回放,省一个分区和一类掉电损坏面。
若 0.9.0 之后确实要「离线带码出门」,再按 B 做,并把分区变更和背板 rev A 的 USB 重烧合并成一次迁移。
**已定**:见 [`adr/ADR-003-codebook-raw.md`](adr/ADR-003-codebook-raw.md)(0.8.0 码库零改动,raw 不进 NVS)。

## 6. `.sub` / `.ir` 互操作(0.8.x)

本仓库 0.7.x 把 `.sub` 列为「不做」([`00-mvp.md`](00-mvp.md) §8),理由是「`Protocol/Bit/Key/TE`
到本项目 4B 的映射未实测」;0.8.x 只读导入已由 ADR-003 定。上游把这条路的语法跑通了,
可以把「未实测」变成「有格式、有测试」。

**`.sub` Key File**(协议型):

```text
Filetype: Flipper SubGhz Key File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: CAME
Bit: 24
Key: 00 00 00 00 00 12 34 56
TE: 390
```

`Key` 固定 8 字节、大端拼成 uint64(24 位协议只用低 3 字节);`TE` 可省略,缺省按协议默认值。
行内不要写 `#` 注释——解析器只认 `Key: value`,`#` 只在行首安全。

**`.sub` RAW File**(原始录制):`Protocol: RAW` + `RAW_Data: -320 640 -640 320 ...`,
**正 = 载波开(high),单位 µs**;可多行,**多行是追加不是覆盖**;空格或逗号分隔。

**`.ir`**:`Filetype: Flipper Infrared File`;每个 `name:` 起一帧;`type: raw|parsed`;
raw 用 `frequency` + `data:`(µs 边沿,mark 开头);parsed 用 `protocol` + `address`/`command`
(4 字节 hex,小端组装)。多帧(空调)必须保序。

实现约束(来自上游):
- `Preset` 字符串必须**精确匹配**已知名,否则拒绝回放,不要猜。
- 超过 32,767µs 的段在 RMT 路径要拆;`.sub` 里可以原样保留 32 位有符号值。
- 文件在**边缘**解析成内部脉冲模型;射频核心只认 `int32 ±µs`,不认识 Flipper。

**验收**:用 Flipper 实际生成的文件(Key + RAW)导入→回放→再导出,`Frequency/Preset/Bit/TE/脉冲`
逐字段 round-trip;不要用自己导出的文件自证。

## 7. 0.9.0 MQTT / Home Assistant

上游的做法是「**每个槽位一个 button 实体** + 一个 `select`(当前主页)+ 一个电池 `sensor`」,
Discovery 配置全部 retained,空槽位用**空 payload 删除实体**。可直接采用的机制:

| 机制 | 上游做法 | 本仓库用法 |
|---|---|---|
| 配置 topic | `homeassistant/<component>/<node>/<object>/config`,retained | 同构,`<node>` 用 MAC |
| `unique_id` | `mynova_rfc_<mac>_<object>` | `rflab_<mac>_<object>` |
| 设备块 | `identifiers/name/model/manufacturer/sw_version/configuration_url` | 必填;`configuration_url` 指设备页 |
| 可用性 | 单独 `availability` topic + LWT(retained) | rf-lab 已有 `/availability` 规划 |
| 删除实体 | 对 config topic 发空 retained | 槽位清空时必做,否则 HA 留僵尸 |
| HA 重启 | 订阅 `homeassistant/status` == `online` 后重发 discovery | 必做,否则重启 HA 丢实体 |
| 状态 | 电池 30s 一推,主页变化才推 | 低频,别把每次 ACK 都推 |
| 命令 | `payload_press: PRESS`;命令 topic 只做一行既有命令 | **payload 仍是 `SEND 1`**,不新增第二套语义 |

**与上游不同的选择(2026-11-24 已定)**:
- topic 用本仓库已定的 `rflab/<node>/{state,event,cmd,availability}`,Discovery 配置才进 `homeassistant/...`。
  上游把 `homeassistant/button/<node>` 同时当**所有实体**的命令/状态前缀(`select`/`sensor` 的
  `command_topic`、`state_topic` 也挂在这个前缀下),只是 `button` 恰好同名才没暴露问题;换一个
  component 就会静默断链。**基础前缀与 Discovery 前缀必须解耦。**
- **不要 200 个实体**:上游 100 RF + 100 IR 全量发布,每次发现最多 200 条 retained 配置,
  首次连接还要逐条发空 payload 清理空槽。本仓库 64 条码 = 64 个 button 已经不轻;
  **已定**:只发布 **8 个快捷位 + `send_last`/`stop` + 传感器**,其余靠 `cmd` 的 `SEND <id>`;
  「全码库实体」只作为显式开关,默认关。
- 电池实体上游已带 `device_class: battery`、`state_class: measurement`、`unit_of_measurement: "%"`,
  直接沿用;`select` 的选项字符串是 `"<n>. <name>"`,命令回来时要再校验一次并重发真实状态,
  以修复 Broker 上的 stale retained。
- Broker 只开局域网、ACL 限 topic、文档写明「MQTT 不是鉴权替代品」——沿用 [`02-plan.md`](02-plan.md) §5.4。

## 8. Web 交互(0.7.3/0.8.0 可直接做)

上游前端是 Vue/Vuetify(资源放 1.8MB LittleFS 分区、无 gzip、纯轮询),**栈不能学**;但这几个交互与协议设计是设备无关的:

| 模式 | 上游做法 | 本仓库用法 |
|---|---|---|
| 上传进度 | XHR `upload.onprogress` 画百分比(焦点请求不走 axios) | 0.7.3 浏览器上传固件必须有;SSE 带不了上传进度 |
| 重启后再连 | POST → 等 1.5–2s → 每 3s `GET /api/status`,最长 120s;全屏遮罩;成功/超时两套文案 | 0.7.3 OTA 完成后的等待页,直接照 |
| 采集占用 | 状态里带 `owned`;浏览器发现 `!receiving && !owned` 就静默回 idle,不报错 | 本仓库有 Web/BLE/串口/watch 四个入口,更需要「谁在前端」标志 |
| 离线判定 | 轮询失败即离线,不另设 health 端点 | 本仓库有 SSE:用心跳超时判定,不新增端点 |
| 状态聚合 | `/api/status` 一次返回 15+ 字段(版本/序列号/电池/内存/网络/计数) | `/api/state` 已同路,保持;不要拆成多端点 |
| 危险操作 | 恢复出厂要输入 `YES`;恢复备份分「信号/配置」两个勾选 | 0.7.3 的 ARM 已更强;备份若做,按类别分 |
| 名称校验 | 按 UTF-8 **字节数** ≤31,不是字符数 | AC-23 已做;保持 |

结论:**本仓库「无框架单页 + gzip + SSE」的方向是对的**。上游要树摇图标字体才能塞进 1.8MB 分区,
且 OTA 只更新 app、改网页必须整机重烧——不要退回框架 + 文件系统方案。

## 9. 主机测试:驱动无硬件也能测

上游的 seam 设计比实现更值得拿:

1. **寄存器级 SPI 模型**:`radio_stubs/SPI.h` 用真寄存器文件建模 header 解码、burst 自增、
   status 读、PATABLE 索引、`MARCSTATE`(SRX→0x0D、STX→0x13、SIDLE→0x01)、CHIP_RDYn(MISO),
   还能注入 `failRx`、`dropWriteAddr`、`stuckReady`。**这就是「配置是否真的写进芯片」的主机证据。**
2. **真实抓包 fixture**:`.json` 存出处 + `payload_hex` + 注释,生成 `.h` 的 `int32` 数组;
   回放时逐边沿喂解码器。上游用「433.92 成功」「418 观察」「重试」四组样本覆盖双帧/噪声。
3. **虚拟时钟 + 引脚归属断言**:`micros/millis` 由测试推进,`delay*` 推进时钟;`pinMode/digitalWrite`
   断言 `!spiPins[p]` —— 正好回归「SPI 引脚被当普通脚」这类错误。
4. **发射波形可验证**:把 RMT item 还原成 `(level,duration)` 与期望逐段比较,而不是只看「没报错」。
5. **静态表 lint**:runner 里重新解析 `CC1101Driver.h` 的 `#define` 和预设表,断言**任何预设都不写
   `MDMCFG0` 或 `FREQ` 寄存器** —— 这正是「把 `MDMCFG0` 的值写进 `0x0E`(其实是 `FREQ1`)」那次事故的回归。
   `tools/` 里已有 `gen_wiring.py --check` 这类静态校验,加一条同类检查成本很低。

**反面**:这些原生测试**没有进 CI**,release workflow 只构建不跑测试。本仓库的 11,312 C checks
已经在 CI 守门,不要把「有测试目录」当成「测试被守门」。

**不要照抄的部分**:上游用「从生产 `.cpp` 里按函数名切出方法体、生成 `.inc`」的方式复用代码
(`tests/native/run_radio_tests.py`)。它能测真实代码,但解析脆弱、重构即断。
本仓库已有 mock HAL 计划([`02-plan.md`](02-plan.md) §5.3.4),应当**把驱动写成可注入 SPI/RMT 接口**,
而不是切源码。

**0.8.0 至少要有的断言**:预设寄存器逐项回读;RX 进入 0x0D / TX 进入 0x13;超时路径返回失败且不发命令;
PATABLE 两字节与 FREND0;长段拆分与奇数原子补位;双帧才确认、单帧不确认;`.sub` round-trip;
取消/超时能在限定时间内退出。

## 10. 打包与发布

上游 `scripts/package_firmware.py` 做了本仓库缺的几件事:

- **编译出的分区表必须与 `partitions.csv` 逐字节一致**(用 `gen_esp32part.py` 重生成再比对);
- 分区唯一、不重叠、不越 8MB;Web 资源必须小于文件系统分区;
- 产出 **app-only OTA 镜像** 与 **整机工厂镜像**,各自 `.sha256` 边车;
- 整机镜像烧到 `0x000000`,会覆盖 NVS/配置 —— 文档明确写「先备份、不能用于 Web OTA」。

但上游只做到「生成哈希」:**设备端从不读 `.sha256`**;设备侧在线升级虽解析 manifest 的 `crc32`,
却**从不比较**(`OTAUpdater.cpp:137` vs `:251-262`),HTTPS 还用 `setInsecure()` 不校验证书。
也就是说,它的完整性校验是「人看的」,不是「设备验的」。

本仓库 0.7.2 已补上:Release 同时附 `firmware.bin` + `SHA256SUMS.txt`;0.7.3 的 Pages manifest
还需要 `sha256` + `size`。
**采用顺序**:先把「发版附 `firmware.bin` + `SHA256SUMS.txt`」补上(不改代码),再在一键更新里消费 manifest。
**不要**采用整机镜像覆盖 NVS 的做法;那只能作为工厂/救援路径,并且必须单独写清楚。

## 11. 不吸收清单

| 不吸收 | 原因 |
|---|---|
| UI 引擎/页面/动画/7 款游戏(GUI+Pages 约 28.5k 行,含游戏 7,674) | U8g2 + 12 键 + PSRAM,与 72×40 无键平台不匹配 |
| Arduino 服务层(`String` 满天飞、`AsyncWebServer` 单文件 1,627 行) | C3 RAM 与可维护性都不允许;本仓库是 C |
| 音频/录音/对讲/TF 卡/3D 外壳 | 不在产品范围 |
| 无 SHA-256 的 OTA(`Update.end` 只校验镜像头) | 本仓库要求 SHA-256;不要退步 |
| 44 条路由零鉴权,固件上传对任何 LAN 客户端开放 | 上游默认 AP 口令仍是 `MYNOVA123`;本仓库 0.7.3 规划的 BOOT arm 正是为此 |
| `GET` 带副作用(连接 Wi-Fi / 连 MQTT / 改日志级别) | 预取/重试会误触发网络变更;改 POST |
| 无 gzip、无 `Cache-Control`/`ETag`,纯轮询 | 本仓库 gzip 单页 + SSE 更省 |
| 测试目录不进 CI | 「有测试」不等于「测试守门」 |
| 整机镜像直接覆盖数据区 | 会清 NVS/配置;只能做工厂路径 |
| 备份包含 Wi-Fi/MQTT 明文凭据 | 若做备份,凭据必须可选/加密/默认排除 |
| 每槽位一个 HA 实体(最多 200 个) | retained 配置膨胀;改用快捷位 + `SEND <id>` |
| 硬编码 OTA 域名占位符 | 上游 `mydomain.com` 仍在代码里;本仓库用 Pages manifest |
| 文件头 `All rights reserved` + GPL LICENSE 并存 | 合规反面教材,不要模仿 |
| RSSI 固定 −74dBm 偏移 | 315MHz 会偏;按频点标定 |

## 12. 落地顺序与工时

**前提:0.7.2 的 P0 未关闭前不写 0.8.0/0.7.3 的新能力代码(M1a 纯重构除外)**([`02-plan.md`](02-plan.md) §0)。

| 版本 | 动作 | 依赖 | 估算 |
|---|---|---|---|
| 0.7.2(已发布,P0 未关) | 本文 + ADR-001/003 草案(已完成) | 无 | ✅ 0.5 天 |
| 0.7.3 | Pages manifest 带 `sha256/size`;Release 固件资产已由 0.7.2 补齐 | 0.7.2 发版 | 1 天 |
| 0.8.0 M1 | `radio` HAL 边界(§4.1)+ YS 回归零变化 | ADR-001 | 3–5 天(M1a 发射 ✅) |
| 0.8.0 M3 | CC1101 原始 RX/TX:预设、RMT 接收、GPTimer 回放、灵敏度 | 背板/舱位 | 2–3 周 |
| 0.8.x | `.sub`/`.ir` 边缘适配 + round-trip 测试 | M3 | 3–5 天 |
| 0.8.x(可选) | 协议解码(先 2 个:EV1527/Princeton) | M3 + ADR-003 | 2 周 |
| 0.9.0 | MQTT + HA Discovery(§7) | Wi-Fi 稳定 | 1–1.5 周 |

> 估算是**单人、含主机测试、不含上机排障**的量级;真机共存(Wi-Fi/BLE + CC1101)与天线调谐另算。

## 13. 决策归属

D1(清洁室)、D2(RAW 只存 RAM)、D3(HA 实体粒度)、D4(`.sub` 只读导入)已经落到
[ADR-001](adr/ADR-001-radio-hal.md)、[ADR-003](adr/ADR-003-codebook-raw.md) 与
[`02-plan.md`](02-plan.md) §5.3–5.4;本文不重复结论,只保留上游事实与复用判断。

## 14. 证据与复现

- 上游:本地只读克隆 `/home/myf/DSH/refs/MYNOVA-RFC-V2`,commit `2ada17f5907b197b3058daf798a1e811579e2a9c`
  (2026-11-22),**不入本仓库**。`git clone --depth 1`,2026-11-24。
- 本文依据:README/README_CHS、`partitions.csv`、`scripts/package_firmware.py`、
  `src/Lib/{CC1101Driver,RFSubGhzPresets,RFSignalDecoder,OokRmtCapture,RawCaptureSession,RFProtocol*,FlipperSignalFile}.*`、
  `src/Drivers/RadioHelper.cpp`、`src/Services/{DataStore,WebService,HAManager}.cpp`、`tests/native/**`。
- 统计:`find ... | xargs wc -l` = **54,762 行 / 265 文件**(C/C++ 264 文件 54,549 行 + `.ino` 213 行);`firmware/src/*.c` = **5,806 行**。
- **未验证(不得写成已吸收/已通过)**:本机没有编译上游、没有 C3 上的 CC1101 实测、
  没有 Flipper 文件 round-trip、没有 HA 集成。§4 的寄存器值是**待本仓库实测**的输入,不是结论。
- 复现命令(只读):

```bash
git -c http.proxy=socks5h://127.0.0.1:10808 clone --depth 1 \
  https://github.com/Tomosawa/MYNOVA-RFC-V2 /tmp/mynova
cd /tmp/mynova && git rev-parse HEAD
grep -n 'Preset\|MDMCFG\|PATABLE' MYNOVA_RFC_V2/src/Lib/RFSubGhzPresets.h
```
