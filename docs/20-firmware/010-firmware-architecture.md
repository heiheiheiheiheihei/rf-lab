# 固件架构(ESP-IDF)

状态: draft · 最后更新:2026-09-20 · 上游依据: [`docs/00-overview/010-product-definition.md`](../00-overview/010-product-definition.md)、[`docs/00-overview/020-architecture.md`](../00-overview/020-architecture.md)、[`docs/00-overview/040-verified-platform-facts.md`](../00-overview/040-verified-platform-facts.md)、[`docs/30-protocol/010-device-app-protocol.md`](../30-protocol/010-device-app-protocol.md)、[`docs/adr/0001-frequency-coverage-gap.md`](../adr/0001-frequency-coverage-gap.md)、[`docs/10-hardware/010-system-architecture.md`](../10-hardware/010-system-architecture.md)、[`README.md`](../../README.md)

> 标注约定:【核实】= 有公开资料支撑;【建议】= 本仓库设计判断;【待确认】= 实现前须核对芯片数据手册/ESP-IDF 版本。
> **本文件是设计文档,不含实现。** 所有代码片段仅表达**接口形状与职责边界**。

---

## 1. 分层与组件划分

### 1.1 目录结构(ESP-IDF component 组织)

```
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults          # 目标 esp32s3 / 16MB flash / OPI PSRAM / 240MHz
├── partitions.csv              # 见 §7
├── main/
│   └── app_main.c              # 仅做初始化编排与任务创建,不含业务逻辑
└── components/
    ├── rf_hal/                 # SPI 串行化 + GPIO/EN 抽象;唯一允许碰 esp_driver_spi/gpio 的层
    ├── rf_types/               # 纯数据结构(频率/调制/结果),无依赖,供全体引用
    ├── rf_driver_si4463/       # 实现 rf_mod_iface_t
    ├── rf_driver_cc1101/       # 实现 rf_mod_iface_t
    ├── rf_driver_nrf24/        # 实现 rf_mod_iface_t
    ├── rf_registry/            # 槽位→驱动实例绑定、探测、活动模块唯一性
    ├── rf_capture/             # RMT RX/RX-DMA、max_edges 管控、RLE 编码
    ├── rf_scan/                # 扫频状态机、coverage 计算、band_clipped 判定
    ├── rf_tx/                  # 发射编排 + 强制门禁(唯一发射出口)
    ├── features/               # 特征提取(纯函数,可 host 编译)
    ├── proto/                  # 帧编解码 + 分片重组(纯逻辑,可 host 编译)
    ├── transport_ws/           # WebSocket 服务端(承载 §2 帧)
    ├── transport_ble/          # GATT 服务(承载 §2 帧 + FRAG 头)
    ├── session/                # IDLE/READY/SCANNING/CAPTURING 状态机 + 限流
    ├── store/                  # 信号库(NVS/分区)
    ├── audit/                  # 发射审计环形日志(只写不删)
    └── policy/                 # 编译期白名单/功率上限/令牌校验(HMAC)
```

### 1.2 依赖方向(必须单向,不得循环)

```
                      policy   rf_types   features(*)   proto(*)
                         │         │          │            │
   rf_hal ──► rf_driver_* ─┴─► rf_registry │    │            │
     │            │              │        │    │            │
     │            └──► rf_scan ──┤        │    │            │
     │            └──► rf_capture┘        │    │            │
     │                     │             │    │            │
     └────────────► rf_tx ─┴─────────────┴────┴────────────┘
                                 │
                             session ──► store / audit
                                 │
                    transport_ws / transport_ble
                                 │
                              main(app_main)

  规则:箭头 = "依赖/可以调用"。下层**绝不**回调上层的具体类型;
  跨层通知统一走事件队列(未画出的 event_bus),而非直接函数调用。
  (*) features / proto 为纯逻辑组件:只依赖 C 标准库与 cJSON,
      不依赖 ESP-IDF 外设驱动 → 可在 host 上跑单元测试(见 §10)。
```

**三条硬规则【建议】**:

1. **`rf_hal` 是唯一能调用 `esp_driver_spi` / `esp_driver_gpio` 的组件。** 驱动层只看得到抽象句柄,便于(未来的)换总线或插桩。
2. **`rf_driver_*` 之间互不引用。** 任何"两芯片协同"必须由 `rf_scan`/`rf_tx` 编排。
3. **`proto` 不知道传输的存在。** 分片与限流由 `transport_*` + `session` 承担,`proto` 只做字节。

---

## 2. 统一模块驱动接口

【设计意图】三个芯片的寄存器模型差异极大,但**对本产品有价值的操作是同一组**。统一接口让上层(`rf_scan`/`rf_capture`/`rf_tx`)完全不需要 `if (chip == ...)`。

```c
/* rf_types/rf_modes.h —— 仅接口形状,不是实现 */
typedef struct {
    uint32_t f_hz;              /* 实际写入芯片的频率 */
    uint32_t f_requested_hz;    /* 上层请求的频率,便于 WARN{band_clipped} */
    int8_t   power_dbm;         /* 档位语义,非精确标定(非目标) */
    int8_t   channel;           /* nRF 私有通道等 */
} rf_tune_t;

typedef struct {
    uint32_t      symbol_rate_bps;   /* OOK/2FSK 符号率 */
    uint32_t      fdev_hz;           /* 2FSK 频偏 */
    bool          fec;               /* CC1101 的 FEC/交织 */
    const char   *mod;               /* "ook" | "2fsk" */
} rf_modcfg_t;

#define RF_MAX_BANDS 4

typedef struct {
    bool      present;                  /* probe 结论 */
    /* 模块级**真实**频段(非芯片手册值);CC1101 有三段,故上限取 4 */
    uint32_t  bands[RF_MAX_BANDS][2];   /* [f_start_hz, f_stop_hz] */
    uint8_t   band_count;
    int8_t    tx_max_dbm;
    uint8_t   spi_bus;                  /* 由 rf_hal 分配 */
} rf_caps_t;

/* 统一接口:每个芯片一个实例,函数指针表 */
typedef struct rf_mod_iface {
    const char *chip;      /* "si4463" / "cc1101" / "nrf24l01p" */
    const char *drv_ver;

    /* 探测:只读 ID/版本寄存器,不改芯片状态 */
    esp_err_t (*probe)     (void *ctx, rf_caps_t *out);
    /* 初始化:复位 → 配置 GPIO/中断 → 进入 IDLE */
    esp_err_t (*init)      (void *ctx);
    /* 调谐:写频率合成器;超出模块能力返回 ESP_ERR_INVALID_ARG */
    esp_err_t (*set_freq)  (void *ctx, const rf_tune_t *t);
    /* 调制参数:编码方式/速率/频偏 */
    esp_err_t (*set_mod)   (void *ctx, const rf_modcfg_t *m);
    /* RSSI:返回已换算为 dBm 的当前值 */
    esp_err_t (*get_rssi)  (void *ctx, float *dbm_out);
    /* 原始发射:上层已通过门禁;此处只做芯片动作,不含策略判断 */
    esp_err_t (*tx_raw)    (void *ctx, const uint8_t *rle, size_t n, uint8_t repeats);
    /* 原始接收:仅用于需要芯片内部 FIFO 的模式,非 RMT 主路径 */
    esp_err_t (*rx_raw)    (void *ctx, uint8_t *buf, size_t *n, uint32_t timeout_ms);
    /* 立即静默:任何错误路径都必须可调用,且必须可在 ISR 上下文外安全重入 */
    esp_err_t (*enter_idle)(void *ctx);

    void *ctx;             /* 驱动私有状态 */
} rf_mod_iface_t;
```

| 职责边界 | 说明 |
|---|---|
| 驱动**做** | 寄存器读写、时序等待、dBm 换算、芯片内 FIFO/包处理配置、错误映射为 `esp_err_t` |
| 驱动**不做** | 频段/功率策略判断、审计、JSON、日志刷盘、跨芯片协同、RMT |
| 上层**必须** | 调谐前用 `rf_caps_t.bands` 夹取并回 `WARN{band_clipped}`;发射前过 `policy` |

> ⚠️ 陷阱:`tx_raw` 只负责"把序列交给芯片"。**安全门禁不能放在驱动里**,否则新增驱动就绕过了限制。唯一出口在 `rf_tx`(§8)。

---

## 3. RMT 捕获设计

### 3.1 为什么用 RMT

OOK/ASK 信号在解调器输出端就是**数字电平跳变序列**,而 ESP32-S3 的 **RMT 天生是"边沿 + 时长"记录器**,【核实】8 通道(4 RX / 4 TX)、支持 DMA。协议契约 §3.5 的载荷正是 RLE 跳变序列(`dt_us` + `level`)——**RMT 的产物可以直接映射成契约载荷,中间不需要采样数组**,这是本设计最省带宽的一环。

### 3.2 通道与内存分配【建议】

| 资源 | 分配 | 理由 |
|---|---|---|
| RMT RX 通道 0 | 主捕获通道(OOK/2FSK) | 只用 1 个通道即可,单活动模块 |
| RMT RX 通道 1 | 预留(外部解调器输入) | 若某模块需要外部比较器(§3.4) |
| RMT TX 通道 0 | 发射(复现 RLE 序列) | 与 RX 不同通道,可并存 |
| 符号池 | 2 × 8K symbols(乒乓) | 每 symbol 4B → 8K symbol ≈ **32KB/块** |
| 存储位置 | 符号池放 **PSRAM**(DMA 可达),控制结构放内部 SRAM | 内部 SRAM 需留给 Wi-Fi/BLE 协议栈 |

`rmt_receive()` 的缓冲是**固定容量**的:在调用前必须算出容量上限。容量 = `min(req.max_edges, 本机 max_edges, 池容量)`。

### 3.3 如何避免 RMT 内存耗尽(核心设计)

三层防线【建议】:

1. **编译期上限**:`#define RMT_MAX_EDGES_HARD 16384`(与契约 `CAPS.limits.max_edges = 16384` 对齐)。16384 × 4B = 64KB × 2(乒乓)= 128KB PSRAM,可接受。
2. **请求期钳制**:`CAPTURE_START.max_edges` 超过硬上限时,**不拒绝**,而是钳到硬上限并在 `CAPTURE_DONE` 中置 `truncated:true`(契约 §3.4 已有该字段)。
3. **运行期溢出策略**:
   - 触发条件:`req.max_edges` 达到,或 `timeout_ms` 到期,或半满块已被消费完毕。
   - 动作:`rmt_rx_stop()` → 立刻把已收 symbol 转成 RLE 并推入**捕获环形缓冲** → 立刻重启接收(开启新块)。
   - **绝不"边收边拼大数组"**:必须流式转换、流式投递,峰值内存与 `max_edges` 成正比而非与总时长成正比。
4. **采集任务被阻塞时的兜底**:若环形缓冲水位超过高水位线,丢弃**最旧**的块(与契约 §2.4 "丢旧保新"一致)并累加 `WARN{throttled}` 计数。

### 3.4 OOK 与 2FSK 的处理差异(**硬件前提必须先讲清**)

| 模式 | 前端路径 | RMT 看到什么 | 契约 FLAGS |
|---|---|---|---|
| **OOK/ASK** | 模块解调器数据输出(如 Si4463 GDO / CC1101 GDO0)→ 直连 GPIO | 真实的包络电平序列 | bit0 = `0` |
| **2FSK** | 模块的 RSSI/模拟输出或解调输出**必须先经过比较器/过零整形**变成数字方波,再进 RMT | 相位/过零序列(不是幅度) | bit0 = `1` |

> **这是硬件前提,不是软件可补的**:RMT 只能采"数字边沿"。若模块没有可用的数字解调输出,就必须在载板上加**比较器(带可调阈值/迟滞)**和外部解调器,把模拟波形整形为数字。**否则 `mod:"2fsk"` 无法实现。** 该前提须与硬件文档 §4 的引脚表联动确认(H-Q4)。
>
> 对 Si4463/CC1101 这类**自带数字解调输出**的芯片,OOK 路径可直接用;2FSK 若使用芯片内部解调器输出"已判决数据",则其语义是**位流**而非**过零序列**——两者在 `features` 里的解释必须显式区分,不能混用同一个 `mod` 值。**这一点需要在契约中明确(见文末待决问题)。**

---

## 4. 扫频设计

### 4.1 参数权衡

| 参数 | 取值方向 | 权衡 |
|---|---|---|
| `step_hz` ↓ | 不漏窄带信号 | 线性增加点数与总时间 |
| `step_hz` ↑ | 快 | 可能**跨过**窄带信号(如 25kHz 带宽的遥控器) |
| `dwell_ms` ↓ | 快 | RSSI 未稳定就读 → **读数偏低/漏检** |
| `dwell_ms` ↑ | 稳 | 线性变慢 |

**经验规则【建议】**:`step_hz` 不要大于目标信号带宽的 1/2–1/3;`dwell_ms` 必须 **≥ 该芯片的调谐建立时间 + RSSI 更新周期**,否则测的是"上一个频点的残余"。

### 4.2 单点耗时从哪里来(逐项拆解)

| 阶段 | 耗时来源 | 量级【待确认,须以数据手册/实测为准】 |
|---|---|---|
| 1. 写频率寄存器 | SPI 事务(几字节 @4MHz ≈ 数 µs) | ~10µs |
| 2. **PLL 调谐建立** | 频率合成器锁定 + 功率放大器/滤波器建立 | **Si4463:数百 µs 量级;CC1101:几十 µs 量级;nRF24L01:约 130µs 级** |
| 3. **RSSI 更新周期** | 芯片内部 RSSI 滤波/平均的刷新率 | **与芯片滤波带宽相关,通常几十–几百 µs** |
| 4. 读 RSSI 寄存器 | SPI 事务 | ~10µs |
| 5. 阈值比较与上报 | 若超阈则入 `SCAN_HIT` 队列 | ~0,受限流约束 |

**关键**:耗时主要来自 **2 和 3**,而不是 SPI。所以"快速扫描"的抓手是:**选对 `step_hz` 与 `dwell_ms`,而不是优化 SPI 时钟**。

### 4.3 估算(以 433.92MHz 附近为例)

> 假设单点综合开销 `T_point = dwell_ms + T_tune + T_rssi_read`。以下 `T_tune+T_rssi_read` 取 **【推算值,待实测】400µs**(Si4463 侧),这是**推算不是实测**,实现阶段必须用示波器/日志校准。

| 场景 | 范围 | 步进 | 点数 | dwell | 总时间估算 |
|---|---|---|---|---|---|
| A 窄带精扫(找遥控器) | 432.92–434.92MHz(±1MHz) | 25kHz | 81 | 1ms | 81 × 1.4ms ≈ **0.11s** |
| B 同上,更保守 | 同上 | 25kHz | 81 | 5ms | 81 × 5.4ms ≈ **0.44s** |
| C CC1101 全子带 | 387–464MHz | 100kHz | 771 | 1ms | 771 × 1.4ms ≈ **1.1s** |
| D CC1101 全子带 | 387–464MHz | 25kHz | 3081 | 1ms | 3081 × 1.4ms ≈ **4.3s** |
| E 779–928MHz 全带 ⚠️**当前不可执行** | 149MHz 宽 | 25kHz | 5961 | 1ms | ≈ **8.3s** |
| F 同上,粗扫 ⚠️**当前不可执行** | 同上 | 200kHz | 746 | 1ms | ≈ **1.0s** |

> ⚠️ **表 E/F 的场景在当前硬件下不可达,仅供架构参考。** 按 ADR-0001 §5.1,硬件已定稿为三模块、不增补,可扫区间仅 **387–525MHz**:CC1101 模块被裁剪到 387–464MHz、SI4463 模块被裁剪到 425–525MHz,**两者都到不了 779MHz 以上**。保留这两行是因为它们代表"最坏情形下的点数规模",对**限流预算与内存规划的论证仍然有效**(表 D/E 的量级正是 §4.4 隐性瓶颈的来源);但**不要**把它们当作可实现需求。同理,任何"119–1050MHz 全扫"的时间估算都无意义。

**结论【建议】**:`25kHz / 1ms` 在**可扫区间(387–525MHz)**内是**亚秒级到数秒级**,满足"快速扫描"的直觉;但**跨子带全扫不可能"秒级完成"**(表 D 已是 4.3s)。

### 4.4 限流是本设计的隐性瓶颈(**重要发现**)

契约 §2.4 规定 `SCAN_HIT` 必须受 **50 msg/s** 限流。若某环境噪声大、阈值设低,一次 4.3s 的扫描(表 D)理论上可产生数千个 hit,会被限流截断到约 **215 条**,其余**丢旧保新**。后果:**App 看到的是"扫描结果被悄悄丢掉一部分"**。缓解【建议】:

1. `SCAN_PROGRESS.hits` 回传**真实命中总数**,而 `SCAN_HIT` 只发**最强的 N 个**(N 由固件按 `stream_rate_hz` 预算推导);
2. 在 `SCAN_DONE` 后由 App 按需 `GET` 完整 hit 列表(压缩后走二进制帧)。
   → 这需要**新增协议消息类型或字段**,已在文末列为契约改动项。

---

## 5. 任务与并发模型

| 任务 | 优先级 | 绑定核心 | 栈 | 职责 | 阻塞点 |
|---|---|---|---|---|---|
| `rf_capture_task` | **最高(高)** | **Core 1** | 4KB | 等 RMT 完成事件 → 转 RLE → 投递环形缓冲 | RMT 信号量/队列 |
| `rf_scan_task` | 高 | **Core 1** | 4KB | 逐点调谐 + 读 RSSI + 判阈 + 发 hit | `rf_hal` 互斥量 |
| `rf_tx_task` | 高 | **Core 1** | 4KB | 过门禁 → 写审计 → 驱动 `tx_raw` | 门禁/审计 IO |
| `proto_task` | 中 | Core 0 | 6KB | 帧编解码、分片重组、会话状态机、限流令牌桶 | 队列 |
| `transport_ws_task` | 中 | Core 0 | 6KB | WebSocket 收发 | socket |
| `transport_ble_task` | 中 | Core 0 | 6KB | GATT notify/write | 协议栈回调 |
| `features_task` | 中低 | Core 1 | 8KB | 捕获结束后算特征(可抢占采集) | 队列 |
| `monitor_task` | 低 | Core 0 | 4KB | 温度/供电/看门狗喂狗/Wi-Fi 恢复 | 定时器 |

### 为什么这样分【建议】

1. **射频时序必须在 Core 1 独占。** Wi-Fi/BLE 协议栈由 ESP-IDF 固定占用 **Core 0**(【核实】ESP-IDF 默认 `CONFIG_ESP_WIFI_TASK_CORE_ID=0`),其任务会在任意时刻抢占并长时间占用 Core 0(协议栈内部有大量临界区与中断)。把 RMT 停止/重启、SPI 调谐、TX 序列写入放在 Core 1,可避免"协议栈抖动导致 RMT 符号池被覆盖"这类**只在压力下出现**的错帧。
2. **传输任务留在 Core 0**,与协议栈同核 → 减少跨核缓存一致性开销与 IPI。
3. **`features_task` 放 Core 1 但优先级低于采集**:特征提取是 CPU 密集(直方图/自相关),放 Core 0 会挤协议栈;放 Core 1 又不能让它在捕获窗口内抢 CPU,故优先级低于 `rf_capture_task`。
4. **队列而非共享内存**跨任务通信:唯一例外是 RMT 符号池(必须是 DMA 可见的连续内存)和环形缓冲(单生产者单消费者,用 `xRingbuffer`)。

**Core 1 上禁用阻塞式日志**:`rf_capture_task`/`rf_scan_task` 内**禁止** `ESP_LOGI` 直写 UART(会在 115200 下阻塞数毫秒)。日志一律投递到低优先级 `monitor_task` 的队列。

---

## 6. 特征提取算法设计(设备侧,给 AI 的输入)

输入:跳变序列 `[(dt_us, level), ...]`;输出:契约 §3.6 的 `features`。**全部算法必须可在 host 上编译运行**(纯 `features` 组件)。

| 字段 | 算法思路 | 可信度 |
|---|---|---|
| `pulse_width_us{min,max,hist}` | 对同电平连续段求宽度 → 用**对数分箱**直方图(脉冲宽度跨 2 个数量级,线性分箱会糊)。`hist` 取**归一化后的前 3–5 个主簇占比** | 高(纯测量) |
| `symbol_rate_bps` | 取直方图**最小主簇中心** `/ 1`(最短脉冲 = 1 个符号周期)→ `1e6 / min_pulse_us`;若存在 2:1 / 3:1 簇则取**最大公约数**作为基本时基 | 中(**±5% 量级**,依赖 ≥10–20 个脉冲样本) |
| `encoding_guess` | 按簇比值判定:<br>• 存在 ~1:1 且 ~1:3 两类宽度 → **PWM**<br>• 所有脉冲等宽、靠**间隔**长短区分 → **PPM**<br>• 每个符号周期内必有跳变,且相位在边界翻转 → **Manchester**<br>• 等宽方波、无区分 → **OOK-raw**(不做编码猜测) | **只能给猜测** |
| `preamble_bits` | 从帧首找**最长的等周期交替段**(0/1 交替且间隔稳定) | 中 |
| `payload_bits` | 去掉前导/同步后,按 `symbol_rate` 折算的剩余位数 | 中 |
| `frame_repeats` | 对整段序列做**自相关**,主峰位置 = 帧周期 → `duration / 帧周期` | 中高 |
| `repetition_ms` | 上一步的主峰位置对应的毫秒数 | 中高 |
| `crc_guess` | 对切出的 payload 试算**有限候选集**(CRC-8 poly 0x07 / CRC-16 poly 0x1021 等),看哪一种让重复帧的校验位一致。**只报告"与候选集中的 X 一致",不报告"这就是 CRC"** | **低(强猜测)** |
| `noise_floor_dbm` | 捕获前在同一频点、同 dwell 下取 RSSI 均值 | 高 |

**必须以"猜测"语义输出的字段【建议】**:`encoding_guess`、`crc_guess`、`symbol_rate_bps`(在样本 <20 脉冲时)、`payload_bits`。

**为什么不能断言**:① 无协议指纹库比对时,编码可从多种波形产生,是**多对一**关系;② 03 契约的非目标已明确"不承诺解调任意未知协议";③ 阈值抖动与多径会让簇中心偏移。因此设计上要求:

- 每个猜测字段配 **`*_confidence`(0–1)**,并在证据不足时返回 `null` 而不是瞎猜;
- `features` 的消费者(App/DeepSeek)必须容忍 `null`;
- 契约 §3.6 目前**没有** `*_confidence` 字段 → 见文末待决问题。

---

## 7. 内存与存储

### 7.1 Flash 分区(16MB)**【建议,需与实现阶段核对】**

| 分区 | 大小 | 用途 |
|---|---|---|
| `factory` | 3MB | 应用固件(单镜像) |
| `ota_0` | 3MB | **预留**——本期不做 OTA(契约 §3.8 `OTA_BEGIN` 仅占位) |
| `nvs` | 256KB | 配置、配对长期密钥、标定值 |
| `audit` | 1MB | 发射审计环形日志(**只追加**) |
| `siglib` | 6MB | 信号库(捕获原始 RLE + 特征) |
| `coredump` | 64KB | 崩溃现场 |
| 其余 | ~2.7MB | 余量/未来 |

> 按键/配网等状态**不写 NVS 高频区**,避免磨损。

### 7.2 PSRAM(8MB)分配**【建议】**

| 用途 | 预算 | 说明 |
|---|---|---|
| RMT 符号池 2×32KB | 64KB | 必须 PSRAM(容量),DMA 可见 |
| 捕获环形缓冲 | 512KB–1MB | 流式投递,支持连续捕获 |
| JSON 序列化/反序列化暂存 | 64KB | 避免大 JSON 撑爆内部 SRAM |
| WebSocket 发送缓冲 | 128KB | 高吞吐帧 |
| 特征计算工作区 | 32KB | 直方图/自相关 |
| 余量 | 其余 | 其他组件若允许也可挂 PSRAM |

**内部 SRAM 只留给**:Wi-Fi/BLE 协议栈、FreeRTOS 任务栈、中断、小对象。**禁止**把大缓冲放进内部 SRAM。

### 7.3 信号存储格式

```
[ Header 64B ]  magic/ver/name/created_at/f_hz/mod/rssi_dbm/edge_count/
                duration_ms/crc32    → 便于 LIST_SAVED 时只读头部建索引
[ RLE 体     ]  (dt_us u16 LE, level u8) × N
[ 尾部 JSON  ]  features 快照(设备已算,gzip 可选)
```

**环形日志(audit)**:固定条目大小 + 单调递增序号 + 双块交替擦写(粗粒度磨损均衡);写满后**覆盖最旧**并提供"已丢弃 N 条"计数。审计**只写不删**,App 侧仅可读。

---

## 8. 发射安全门禁(编译进固件的约束)

> 契约 §1「发射默认拒绝」+ §7「`tx_whitelist` 为空 = 禁止一切发射」+ §4 `policy`。**本节是把契约落到代码结构上的方式。**

**唯一出口**:任何发射,包括 `REPLAY_SIGNAL` 与未来的新消息类型,必须经过

```
rf_tx_request(src) ──► [1] 状态检查 ──► [2] 模块在位/活动槽位 ──► [3] 频段白名单
                   ──► [4] 功率上限 ──► [5] 令牌校验(HMAC) ──► [6] 审计预写(同步落盘)
                   ──► [7] 合并 KILL/2.4G 总闸 ──► [8] driver->tx_raw
```

> 与顶层架构 [`020-architecture.md`](../00-overview/020-architecture.md) §3.3 的「设备四道门禁:①白名单 ②功率上限 ③令牌 ④模块在位」是同一约束的展开——**四道门禁是契约下限,本文的 8 步是其在固件中的落点**(前三步为发射前置状态,后两步为审计与硬件门控)。

| 层 | 强制手段 | 为什么无法用"配置项"绕过 |
|---|---|---|
| **[1] 状态** | 非 `READY`/模块就绪态一律 `ERROR{not_ready}`/`module_busy` | 契约 §5 规则 1 已强制 |
| **[2] 模块在位** | 发射前**重新 `probe()`**一次,而非依赖缓存的 `present` | 模块可热插拔/接触不良;缓存会撒谎 |
| **[3] 频段白名单** | 白名单表定义为 **`const` 数组,位于 `.rodata`**,并有独立校验:请求频率必须落在白名单区间 ∪ 模块 `caps.bands` | 运行时没有任何 API 能写 `.rodata`;NVS 里**不存在**白名单配置项,所以"改配置放开"这条路不存在 |
| **[4] 功率上限** | `policy.tx_power_cap_dbm` 由编译期常量**取 min** 后生效;驱动层 `tx_raw` 前再断言一次 | 即使 App 传 `power_dbm: 20`,最终值是 **min(App, cap, caps.tx_max_dbm)** |
| **[5] 令牌** | HMAC(设备长期配对密钥,首次 BLE 配对生成)对 `{nonce, f_hz, power_dbm, mod, exp}` 计算;校验 `exp` 窗口(契约:30s)与 **nonce 重放表**(最近 N 个) | 设备持有密钥,但**不持有任何"关闭校验"的开关**;校验失败直接 `tx_denied`/`tx_bad_token`,**不允许重试**(契约 §6) |
| **[6] 审计** | **先写审计,后发射**:审计写盘成功才允许进入 [7] | 顺序在代码里写死 → **不存在"发出去了但没记录"的路径** |
| **[7] 硬件门控** | `KILL` 引脚(硬件文档 §2.3 GPIO8)+ 2.4GHz 总闸,软件两路都置位才可能发射 | 即使固件被换成恶意版本,KILL 未释放时 PA 轨仍无电 |
| **[8] 唯一性** | `rf_tx` 是唯一包含 `tx_raw` 调用的组件;`rf_driver_*` 不导出"裸发射"给其他组件 | 编译期:`tx_raw` 符号只在 `rf_driver_*` 内部可见 |

**另有两条编译期约束【建议】**:监管域用 `CONFIG_RFLAB_REGION` **构建期**选择,不是运行时参数;`policy` 组件依赖 `audit`,形成"没有审计就无法编译"的依赖链。

---

## 9. 可靠性

| 机制 | 设计 |
|---|---|
| 任务看门狗 | `CONFIG_ESP_TASK_WDT`:`rf_capture`(5s)、`rf_scan`(5s)、`transport_*`(10s)注册;`monitor_task` 负责喂狗,**不代替**被监控任务喂 |
| 射频超时归 idle | `CAPTURE_START.timeout_ms` + **固件侧独立硬超时**(取 `min(timeout_ms, 30s)`);任何超时路径**必须**调 `enter_idle()`,包括错误与 abort |
| 状态机守卫 | 契约 §5 规则 3「发起新任务隐式中止旧任务」→ 由 `session` 统一实现为 `abort_current()` 后再 `start()`;**禁止**各任务自己判断"要不要先停" |
| SPI 异常 | 连续 N 次读写失败 → `push_warning` → 尝试重新 `init()` 该模块(最多 3 次)→ 仍失败则回 `MODULE_STATE{state:"error"}`,不重启整机 |
| Wi-Fi 断连 | 指数退避重连(1s→2s→…→30s 上限);**发射窗口结束后强制校验** Wi-Fi 是否恢复,未恢复则重启协议栈 |
| BLE 断连 | 恢复广播;**不**自动重启整机 |
| 协议栈与射频互斥 | 发射前向 `transport_*` 申请"静默窗口",窗口内有帧待发则**延迟发射**而非丢帧 |
| 异常自愈 | 所有错误汇聚到 `error_sink`,按 `hw_error` / `not_ready` 映射为契约错误码;严重错误触发模块级复位而非系统复位 |
| 掉电安全 | 审计与信号库写入使用"先写临时槽 + 提交标志",避免断电产生半条记录 |

---

## 10. 测试策略

| 层级 | 手段 | 覆盖对象 | 是否需硬件 |
|---|---|---|---|
| **Host 单元测试** | 在 PC 上编译 `proto` / `features` / `store` 索引 / `policy`(纯逻辑),用 pytest+ctypes 或 Unity host runner | 帧编解码、分片重组(含缺片/超时)、限流令牌桶、`features` 各字段、白名单/功率**取 min**语义、HMAC 校验与 nonce 重放 | 否 |
| **特征提取的回归语料** | 把**真实录制**的 OOK/2FSK RLE 序列固化为测试向量(含噪声/弱信号样本),断言字段落在期望区间 | `features` 的稳定性;防止"调参把之前能认的认错" | 否 |
| **协议契约一致性测试** | 从 `protocol/schemas/` 生成校验,断言设备输出**每个** `CAPS`/`SCAN_DONE`/`features` 字段通过 schema;并做**负向**测试(超范围频率必须产生 `WARN{band_clipped}`) | 契约符合性;覆盖诚实性(ADR-0001 的落地) | 否 |
| **HIL(硬件在环)** | 真实板 + 可编程信号源/另一块已知遥控器;串口驱动脚本下发 `START_SCAN`/`CAPTURE_START` 并断言响应 | 驱动 `probe/set_freq/get_rssi`、RMT 捕获时序、SPI 时序、发射门禁的**真实拒绝** | 是 |
| **扫频准确性验证** | 用信号源在已知频点输出已知功率,扫描后断言峰值频点与幅度排序;与商用频谱仪/已知遥控器实测对比(对齐判据 **A-02 `±100kHz`**) | 频率标定、RSSI 线性度、`step_hz` 是否漏检 | 是 |
| **捕获-复现闭环** | 捕获一路 433.92MHz OOK 遥控器 → 特征提取 → 按 `features` 重建 RLE → 发射 → 断言接收端(原设备)响应(对齐判据 **A-03**) | 端到端 | 是 |
| **共存压力测试** | M3 发射的同时跑 Wi-Fi 长连接,统计丢包/重连次数;断言发射窗口 ≤ 预算且 Wi-Fi 能在 N 秒内恢复 | 硬件文档 R-01 的最坏风险 | 是 |
| **长稳测试** | 无人值守 24h:随机扫频/捕获/发射,断言无内存泄漏(周期打印 heap/PSRAM 水位)、无看门狗复位、审计条数 == 发射请求条数 | C-05 无人值守 | 是 |

**测试基础设施【建议】**:HIL 用 `tools/` 下的 pytest 脚本经 USB-Serial-JTAG 直接下发**二进制帧**(绕过 App),保证"设备侧行为"可被独立验证,不被 App 实现影响。

---

## 11. 待决问题

| ID | 问题 | 类型 |
|---|---|---|
| F-Q1 | `rmt_receive` 在 RX 通道上的 DMA 能力与缓冲语义(是否支持"半满中断 + 自动换块")需按 **ESP-IDF 具体版本**确认 | 待确认(实现前必查) |
| F-Q2 | 各芯片 PLL 调谐时间与 RSSI 更新周期的**实测值**(§4.2 的推算依据) | 待实测 |
| F-Q3 | 2FSK 是否需要外部比较器/过零整形(硬件文档 H-Q4) | 待硬件确认 |
| F-Q4 | Wi-Fi 与 M3 发射的**静默窗口**下界(多长的窗口不会掉连接) | 待实测 |
| F-Q5 | ~~`features` 是否需要 `*_confidence`,以及 `null` 的语义~~ | ✅ **已关闭(契约 v1.1 §3.6)**:已规定三级不确定性,`*_confidence`(0–1 或 `null`)与 `null`="拒绝猜测"语义均已写入契约,schema 已同步 |
| F-Q6 | ~~「2FSK 过零序列」与「芯片内部已判决位流」无法区分~~ | ✅ **已关闭(契约 v1.1 §3.5/§3.6)**:二进制载荷 `FLAGS` 增 bit1(判决点),并在 `features` 增 `raw_semantics`(`undecided`/`decided`) |
| F-Q7 | ~~低阈值 + 大范围扫描时 `SCAN_HIT` 被限流截断,缺批量取回消息~~ | ✅ **已关闭(契约 v1.1 §2.4/§3.3)**:新增 `0x23 GET_SCAN_HITS` / `0xA4 SCAN_HITS` 分页取回;`SCAN_DONE` 拆为 `hits_total`/`hits_pushed`/`hits_dropped` 并加 `scan_id` |
| F-Q8 | ~~`CAPS.limits.max_edges` 与 `CAPTURE_START.max_edges` 冲突语义未定义~~ | ✅ **已关闭(契约 v1.1 §3.4)**:明确为**钳制 + `truncated:true` + `WARN{edge_clamped}`**,不得报错拒绝 |
| F-Q9 | ~~BLE 分片上限 15 × ~244B ≈ 3.6KB < 契约 `LEN` 上限 4096~~ | ✅ **已关闭(契约 v1.1 §2.2.1)**:已推导真实上界,并**纠正了原估算的两处口径错误**——原估算用的 `244B` 既偏小又自相矛盾。正确逐项推导:`LL 251 − L2CAP 4 = ATT 载荷 247`;`每片占用 = ATT 操作码 1 + FRAG 头 1 = 2`,故每片净载 **245B**(原 `244` 是按 2B FRAG 头算的,而 §2.3 的 `[FRAG_IDX:4bit \| FRAG_TOTAL:4bit]` 实际只占 **1B**);`× 15 = 3675B`;`− 帧头 6 = LEN 上界 3669B`。新增 `CAPS.limits.ble_frame_max` 作为唯一动态来源,并新增 `ERROR{frame_too_large}` |

> **F-Q5…F-Q9 的处置说明(协调者,契约 v1.1)**:上述五项均由协调者直接修订 `docs/30-protocol/010-device-app-protocol.md` 关闭,`protocol/schemas/` 已同步(新增 `$defs.confidence` / `$defs.guessString` / `$defs.rawSemantics`,features 增 6 个 `*_confidence` + `raw_semantics` + `incomplete`,`limits` 增 `ble_frame_max`,`errorCode` 增 `frame_too_large`,`warnCode` 增 `edge_clamped`;校验 `python3 tools/validate_schemas.py` 退出码 0)。
>
> **但契约修好 ≠ 固件实现好了**:F-Q5/F-Q6 现在对固件提出了**新的实现义务**——特征提取必须真的产出置信度、真的区分判决点、真的输出 `truncated`/`incomplete`。若固件只填默认值(如全部 `*_confidence` 缺失),契约的不确定性表达就落空,AI 侧仍会把猜测当事实。**建议把"每个猜测字段都带置信度"列为固件验收项。**

### 对协议契约的**报告**(未修改契约,仅供协调者决策)

1. **§3.6 `features` 缺 `*_confidence`**:设备侧只能给"猜测"的字段(`encoding_guess`/`crc_guess`/`payload_bits`)没有置信度载体,App/DeepSeek 无法区分"确定"与"瞎猜"。
2. **§3.4/§3.5 `mod` 语义不足**:OOK 电平序列与 2FSK 序列已有 FLAGS 区分,但"过零序列"与"已判决位流"无法区分,而两者的 `features` 解释完全不同。
3. **§2.4/§3.3 限流与结果完整性冲突**:50 msg/s 会把大范围扫描的 hit 静默丢弃,与 §"覆盖诚实性"的精神相悖。建议增加"扫描结果批量取回"的消息或让 `SCAN_DONE` 携带完整 hit 摘要。
4. **§3.4 `max_edges` 冲突语义未定义**:请求值与 `CAPS.limits.max_edges` 冲突时的行为(钳制并 `truncated:true`,还是 `ERROR`)需要写明。
5. **§2 `LEN ≤ 4096` 与 §2.3 `FRAG_TOTAL ≤ 15` 在 BLE 上不相容**:按 MTU 244B 计算单帧上限约 3.6KB,4096B 的帧无法分片。需二选一(降 `LEN` 上限 / 提分片数或引入多帧续传)。
6. **§3.2 `MODULES.spi` 字段语义含糊**:硬件文档最终采用"单 SPI 总线 + 3 CS",三模块的 `bus/miso/mosi/sck` 相同、仅 `cs` 不同;字段够用但需一句说明澄清"总线"含义。
7. **§7 未定义 `auth_token` 的获取/分发机制**:App 侧的长期密钥如何派生、nonce 从何而来,契约只要求"App 计算",缺可实现的定义。
