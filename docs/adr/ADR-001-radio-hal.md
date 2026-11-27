# ADR-001 · 射频抽象接口与 CC1101 接入边界

状态:**接受(0.8.0)** · 2026-11-24 · 决策者:项目负责人
相关:[ADR-003](ADR-003-codebook-raw.md)、[`../04-ref-mynova.md`](../04-ref-mynova.md) §4、[`../02-plan.md`](../02-plan.md) §5.3

> 本文只定「接口与边界」。寄存器细节、预设表、坑位在 [`../04-ref-mynova.md`](../04-ref-mynova.md) §4;
> 实现必须按 TI 数据手册独立写,规则见 §7。

## 1. 背景

- 现役链路是 **YS-RF34DL**(UART 固定码模块):`SEND/LONG/STOP` 已在真机验证,
  码库 64 条、`band + code[4]`、名称/分组/符号已上机([`../00-mvp.md`](../00-mvp.md) §13)。
- 0.8.0 要做「模块抽象 + SPI 背板」,首个新模块是 **CC1101**;0.8.0 只承诺
  **原始 RX/TX**,不承诺固定码学习/发射,也不承诺 `LONG/STOP`(能力矩阵见 [`../02-plan.md`](../02-plan.md) §5.3.2)。
- 硬约束:C3 / 4MB Flash / 无 PSRAM / 单核;app 槽 1.94MB(已用约 1.09MB);
  **传输层只提交既有命令行**,不复制射频时序([`../../AGENTS.md`](../../AGENTS.md) 硬约束 2);
  0.7.2 的 P0 未关闭前不写 0.8.0 代码。

## 2. 决策

### 2.1 分层

```text
传输层(串口 / HTTP+SSE / BLE / TUI / Watch)
        │  只提交既有命令行文本
        ▼
命令路由(main.c)── 码库 / 事件环 / 设置
        │  radio_* 调用(不再直接碰 UART/SPI)
        ▼
radio 门面(状态机、busy、取消、能力)
        ├── backend: ys_rf34dl(UART)
        └── backend: cc1101(SPI + RMT/GPTimer)
```

- 后端只实现「能力」,不实现命令语义;命令语义仍在 `main.c`/协议层,保证 `SEND/LONG/STOP`
  的输出字节不变。
- 传输层**不直接调用** `radio_*`;所有新能力必须先是命令行,再是 UI([`../01-ui.md`](../01-ui.md) §1)。

### 2.2 能力位

| 位 | 含义 | YS | CC1101(0.8.0) |
|---|---|---|---|
| `RADIO_CAP_FIXED_RX` | 模块解出固定码 | ✅ | ❌ |
| `RADIO_CAP_FIXED_TX` | 按固定码发射 | ✅ | ❌ |
| `RADIO_CAP_LONG` | `LONG/STOP` 长发 | ✅ | ❌ |
| `RADIO_CAP_RAW_RX` | 原始脉冲接收 | ❌ | ✅ |
| `RADIO_CAP_RAW_TX` | 原始脉冲发射 | ❌ | ✅ |
| `RADIO_CAP_RSSI` | 读 RSSI | ❌ | ✅ |
| `RADIO_CAP_POWER` | 发射功率档 | ❌ | ✅ |
| `RADIO_CAP_SENS` | 接收灵敏度档 | ❌ | ✅ |
| `RADIO_CAP_SCAN` | 扫频 | ❌ | 0.9+ |

UI 的按钮禁用、官网措辞、能力矩阵都从这张表派生;未置位的能力**不得**在 UI 出现「可用」形态。

### 2.3 类型与接口(草案)

```c
typedef enum {
    RADIO_OK = 0,
    RADIO_ERR_ARG,          /* 参数非法 */
    RADIO_ERR_IO,           /* UART/SPI/GPIO 失败 */
    RADIO_ERR_BUSY,         /* 正在收/发,拒绝新动作 */
    RADIO_ERR_STATE,        /* 状态不允许(如未 init) */
    RADIO_ERR_UNSUPPORTED,  /* 能力位未置位 */
    RADIO_ERR_TIMEOUT,      /* 等应答/等完成超时 */
    RADIO_ERR_EMPTY,        /* 非阻塞轮询:暂无数据(不是错误) */
    RADIO_ERR_FULL,         /* 会话缓冲满 */
} radio_err_t;

typedef enum {
    RADIO_SRC_FIXED = 0,    /* YS: band + code[4];0.8.0 只用这个 */
    RADIO_SRC_PROTO = 1,    /* 0.8.x/v3 预留:freq + preset + bits + key + te */
} radio_src_t;

typedef struct {
    uint8_t  band;          /* 315 / 433(YS 语义) */
    uint8_t  code[4];       /* 地址2/地址1/按键/osc */
} radio_fixed_t;

typedef struct {
    uint32_t freq_hz;       /* 0 = 模块自有频段 */
    uint8_t  preset;        /* RADIO_PRESET_*;0xFF = 默认 */
    int8_t   sensitivity;   /* -1 = 默认;0..2 */
    int8_t   power;         /* -1 = 默认;0..7 */
} radio_raw_cfg_t;

typedef struct {
    const int32_t *pulses;  /* 有符号 µs:正 = 载波开 */
    size_t         count;   /* 1..RADIO_RAW_MAX_PULSES */
    uint32_t       freq_hz;
    uint8_t        preset;
    int8_t         power;
    uint8_t        repeat;  /* 0/1 = 发一遍 */
} radio_raw_tx_t;

typedef struct {
    uint32_t caps;          /* RADIO_CAP_* 位或 */
    uint8_t  preset_count;
    const char *name;       /* "YS-RF34DL" / "CC1101" */
} radio_info_t;

typedef struct {
    const char *name;
    radio_err_t (*probe)(void *ctx, radio_info_t *out);
    radio_err_t (*init)(void *ctx);
    radio_err_t (*deinit)(void *ctx);

    /* 固定码(仅 FIXED_*):非阻塞轮询;无新帧返回 RADIO_ERR_EMPTY,不算错误 */
    radio_err_t (*learn_poll)(void *ctx, radio_fixed_t *out);
    radio_err_t (*send)(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res);
    radio_err_t (*long_start)(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res);
    radio_err_t (*long_stop)(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res);

    /* 原始码(仅 RAW_*):start 后持续写入会话缓冲,read 取新段;无新段返回 EMPTY */
    radio_err_t (*raw_start)(void *ctx, const radio_raw_cfg_t *cfg);
    radio_err_t (*raw_stop)(void *ctx);
    radio_err_t (*raw_read)(void *ctx, int32_t *pulses, size_t cap, size_t *out_count);
    radio_err_t (*tx_raw)(void *ctx, const radio_raw_tx_t *tx);        /* 阻塞,可取消 */
    void        (*cancel)(void *ctx);
} radio_ops_t;
```

约定:

- **脉冲单位一律有符号 µs,正 = 载波开**;与 `.sub` 的 `RAW_Data` 同号,避免两层转换。
- `tx_raw` 的段数上限 `RADIO_RAW_MAX_PULSES`(首版 4096;缓冲 16KB 内部 RAM),
  超过返回 `RADIO_ERR_FULL`;不做静默截断。
- 频点用 `uint32_t Hz`;YS 的 `band` ↔ 具体 Hz 的映射留在 `main.c`,后端不猜。
  315 族取 315.000MHz,433 族的具体值(常见 433.92MHz)在 M3 与 CC1101 一起标定,
  **不写死猜测**;`.sub` 的 `Frequency:` 是绝对 Hz,回放时按文件值,不经过 band。
- 预设编号沿用 [`../04-ref-mynova.md`](../04-ref-mynova.md) §4.2 的 0..3;
  `.sub` 的 `Preset:` 字符串 ↔ 编号的映射放在文件适配层,不放进 HAL。
- 签名是**草案**:实现时可改字段名,不得改语义(尤其符号约定、能力位、错误码)。

### 2.4 状态机与所有权

```text
        probe ──► INIT ──► IDLE
                          │  ├── raw_start ──► RX_RAW ──raw_stop──► IDLE
                          │  ├── long_start ─► TX_LONG ─long_stop─► IDLE
                          │  └── send/tx_raw ─► TX ──完成/超时──► IDLE
                          └── deinit ──► INIT
```

- **单所有者**:同一时刻只有一个后端处于活动状态;跨后端切换必须先 `deinit` 或 `long_stop`。
- **YS 的接收不占状态**:模块常开收,`learn_poll` 在 IDLE 下轮询;只有 `send/long_*`
  进入 TX 状态。CC1101 的 `raw_start/stop` 才显式占 RX_RAW。
- 冲突一律返回 `RADIO_ERR_BUSY`,由命令层翻成 `ERR busy` / HTTP 503;**不阻塞等待**,
  不静默排队。
- CC1101 半双工:`raw_start` 前必须停发;`tx_raw` 内部负责 `IDLE→TX→IDLE`,
  上层不关心寄存器。
- 取消:`tx_raw` 必须能在 ≤100ms 内退出;`LONG` 的取消走 `long_stop`(YS 的 `0xFF`)。
- 进程崩溃/重启后,后端 `init` 必须把芯片置于已知 IDLE;不允许继承上一次的 TX 状态。

### 2.5 频段与能力映射(UI/官网)

| 命令/UI | YS | CC1101 0.8.0 |
|---|---|---|
| `SEND <id>` | ✅ 固定码 | ❌ 返回 `ERR unsupported` |
| `LONG/STOP` | ✅ | ❌ |
| `RAW RX`(会话) | ❌ | ✅ 只存 RAM,见 ADR-003 |
| `RAW TX` | ❌ | ✅ 最长 4096 段 |
| 码库 | 64 条固定码 | 不适用(见 ADR-003) |

## 3. 0.8.0 范围(写死)

- **M1a**(2026-11-24 已实现):YS **发射**路径套进 HAL(SEND/LONG/STOP);
  `LIST/SEND/LONG/STOP` 的输出与串口帧逐字节不变;主机断言帧与 `rf34dl_build_tx` 同源。
- **M1b**(未做):接收解析(`rf34dl_parser_t`)迁入后端,启用 `FIXED_RX` 与 `learn_poll`;
  在那之前 YS 能力位只有 `FIXED_TX|LONG`。
- **M3**:CC1101 只做原始 RX/TX + 功率/灵敏度;不做协议解码、不做 `.sub`、不做持久化。
- **不做**:NRF24L01 / SI4463 / LR1121(各自单独验收)、BLE OTA、云端。
- 代码禁区:0.7.2 的 P0 未关闭前,不写 M1/M3 的实现代码(本 ADR 只是设计)。

## 4. 后果

| 面 | 影响 |
|---|---|
| 正面 | 传输层/码库与射频解耦;新模块只加 backend;能力矩阵可测试 |
| 负面 | 多一层函数指针与状态机,YS 路径多一次转发(可忽略) |
| 风险 | C3 单核下 CC1101 的 ISR/解码会与 Wi-Fi/BLE 抢 CPU;必须在 M3 实测 |
| 风险 | 背板/舱位未定(ADR-002),SPI 引脚与供电可能变;HAL 不依赖具体引脚 |
| 资源 | RAW 会话 +16KB 内部 RAM(4,096×4B);当前静态 RAM 约 90KB,加后仍 <1/3,但要进构建报告 |
| 资源 | 驱动+采集+回放估 30–60KB Flash;app 槽 1.94MB、已用 1.09MB,余量够,但按 M3 实测记账 |

## 5. 干净室与许可边界(D1 的执行)

本仓库**保持非 GPL**(默认保留所有权利)。因此:

- **允许**阅读:TI CC1101 数据手册、DN013/DN022、Flipper 的**文件格式文档**、
  Home Assistant MQTT Discovery 文档、本仓库 ADR 与 [`../04-ref-mynova.md`](../04-ref-mynova.md)。
- **禁止**拷贝/逐行翻译上游 `CC1101Driver.*`、`RFProtocol*`、`RFSubGhzPresets.h`、
  `FlipperSignalFile.*` 等 GPL 文件。
- **写代码的人不得打开上游源码**;所需事实从本 ADR 与 [`../04-ref-mynova.md`](../04-ref-mynova.md) 取。
  若实现者已经读过上游(含本台账的作者),必须在 PR 里注明「非严格清洁室」,并按「独立重写 +
  逐段审查 + 出处核对」执行,不能声称完全隔离。
- 实现者须在 PR 描述里写**出处**:每个寄存器组注明「TI 数据手册 §x / DN0xx / SmartRF 设置」;
  预设表按 TI 重建,不标注「来自 Flipper」。
- 审查项:变量/函数命名、注释、错误文案不得与上游雷同;`git diff` 不接受整段同构代码。
- 若将来确需复用 GPL 代码:**整体许可评估 + 单独 ADR**,不得静默合并。

## 6. 验收(M1a/M1b/M3)

| 级别 | 内容 |
|---|---|
| Host(M1a ✅) | `radio` 门面能力位/状态/BUSY;YS 帧与 `rf34dl_build_tx` 逐字节一致;F0/FE/FF 应答与超时;114 checks |
| Host(M1b/M3) | 接收解析迁入后端;mock SPI/RMT/Timer;预设逐项回读;RX 0x0D、TX 0x13;PATABLE 两字节;长段拆分;取消/超时退出 |
| Device | `PROBE` 能区分空舱/YS/CC1101;CC1101 收到已知遥控器并回放;学习型遥控器能解回;Wi-Fi STA + BLE + CC1101 同时开 10 分钟不崩 |
| 资源 | 记录 Flash/RAM;OTA 槽 <70%;不新增分区(见 ADR-003) |
| 证据 | 写入 `docs/evidence/`;Build/Host/Device 分开报告,未上机项标 Unverified |

## 7. 修订记录

- **2026-11-24(M1a 实现)**:`send/long_start/long_stop` 增加 `radio_tx_result_t *res`
  (返回最后应答与耗时,供 `OK/WARN` 文案与 `ms` 字段);`long_stop` 增加 `frame` 参数——
  YS 的 `0xFF` 帧要携带 code,才能保持旧路径逐字节一致。语义不变。

## 8. 重访条件

- C3 上 CC1101 与 Wi-Fi/BLE 共存失败(丢边沿、看门狗、吞吐掉一半):先降级为
  「Wi-Fi 关闭时才能 RAW」,并记录;不要用提高优先级硬扛。
- 协议解码/`.sub` 导入排期落地:按 [ADR-003](ADR-003-codebook-raw.md) 的 v3 路径走,不改 HAL 语义。
- 背板定型后若 SPI 引脚/供电变化:只改 backend,不改本 ADR 的接口。
