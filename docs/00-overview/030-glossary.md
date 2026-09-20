# 术语表

状态:`draft` · 最后更新:2026-09-20

> **用途**:统一本项目在代码、文档、UI 文案中使用的术语。若发现同一概念出现多种叫法,以本表为准并修正其他位置。

## 1. 射频术语

| 术语 | 英文/缩写 | 定义 | 本项目中的用法 |
|---|---|---|---|
| 接收信号强度 | RSSI | Received Signal Strength Indicator,接收信号强度指示,单位 dBm | 扫频的判据;`SCAN_HIT.rssi_dbm` |
| 噪底 | Noise Floor | 无信号时的本底噪声电平 | `features.noise_floor_dbm`;用于判断信号是否显著 |
| 扫频 | Sweep / Scan | 逐点调谐到不同频点并读取 RSSI 的过程 | **不是**宽带 FFT;速度受调谐+驻留限制 |
| 驻留时间 | Dwell Time | 每个频点停留的时长 | `START_SCAN.dwell_ms` |
| 频率步进 | Step | 相邻扫描频点的间隔 | `START_SCAN.step_hz` |
| 开关键控 | OOK / ASK | On-Off Keying,用有无载波表示 0/1 | 433MHz 遥控器最常见调制 |
| 频移键控 | (2)FSK | Frequency Shift Keying,用两个频点表示 0/1 | 传感器、数传常见 |
| 跳变序列 | Edge Sequence | 电平/相位发生变化的时间点序列 | 本项目的捕获表示形式(RLE 编码) |
| 游程编码 | RLE | Run-Length Encoding,相邻间隔按"间隔+电平"成对存储 | `DATA_CHUNK` 的载荷格式 |
| 符号率 | Symbol Rate | 每秒传输的符号数,单位 bps(此处指码元率) | `features.symbol_rate_bps` |
| 脉宽 | Pulse Width | 单个电平持续的时长,单位 μs | `features.pulse_width_us` |
| 脉宽编码 | PWM / PPM | 用脉宽/脉位区分 0 与 1 的编码方式 | `features.encoding_guess` 的取值之一 |
| 曼彻斯特编码 | Manchester | 每比特中间必有跳变的编码,自带时钟恢复 | `features.encoding_guess` 的取值之一 |
| 前导码 | Preamble | 帧头前的固定同步序列 | `features.preamble_bits` |
| 重复周期 | Repetition | 同一帧重复发送的间隔 | `features.repetition_ms` |
| 模块 | Module | 本项目中指一块可直插的射频子板(如 AS10-M4463D-SMA) | 槽位 1/2/3;`CAPS.modules[].slot` |
| 槽位 | Slot | 设备上模块的物理安装位置编号 | `slot ∈ {1,2,3}` |
| 频段 | Band | 模块能够工作的连续频率区间 | `CAPS.modules[].bands` 是**区间数组**,不是单值 |
| 频段裁剪 | Band Clipping | 请求的扫描范围超出模块能力,被夹取到真实区间 | 必须回 `WARN{band_clipped}`;**禁止静默裁剪** |
| 直连 | — | 手机与设备在同一网络/蓝牙范围内直接通信 | 不经云端中转 |

## 2. 本项目专有概念

| 术语 | 定义 | 反例(不要这样用) |
|---|---|---|
| **活动模块** | 同一时刻唯一可收发的模块,由 `SELECT_MODULE` 指定 | ❌ "同时用三个模块扫频"——不支持 |
| **特征** `features` | 设备从原始跳变序列归纳出的结构化描述(协议 §3.6) | ❌ 不要把原始码流叫"特征" |
| **原始码流** | RLE 编码的跳变序列本身 | ❌ 不要叫"原始数据",容易与 features 混淆 |
| **AI 分析** | App 调用 DeepSeek 得到信号类型/协议/威胁等级的过程 | ❌ 不要说"设备分析信号" |
| **威胁等级** | `info` / `low` / `medium` / `high` / `unknown` 五档 | ❌ 不要新增档位,等级有判据表 |
| **发射门禁** | 设备侧强制执行的频段白名单 + 功率上限 + 令牌三重校验 | ❌ 不要叫"发射配置",它是约束不是配置 |
| **审计日志** | 每次发射请求(含被拒)的本地记录 | ❌ 不要与信号库混淆 |
| **信号库** | 用户主动保存的信号(特征 + 可选原始码流) | ❌ 不要与审计日志混淆 |
| **令牌** | App 计算的短时 HMAC(30s 有效),用于发射授权 | ❌ 不要叫"密码"或"API Key" |
| **能力集** `CAPS` | 设备上报的真实硬件能力,App 的唯一真实来源 | ❌ App 不得硬编码频段/功率 |

## 3. 易混淆对

| 易混淆 | 区别 |
|---|---|
| **扫频 vs 捕获** | 扫频 = 逐点测能量,找"哪里有信号";捕获 = 锁定一个频点,记录时序,回答"这是什么信号" |
| **芯片能力 vs 模块能力** | SI4463 芯片可达 142–1050MHz、CC1101 芯片有 300–348/387–464/779–928MHz 三段,但 **AS10 模块被前端裁剪到 425–525MHz、AS07 模块被裁剪到 387–464MHz**。**协议只认模块能力**(`CAPS.bands` 必须回传模块级真实频段) |
| **ROM 版本 vs 频率能力** | Si4463 的 `C2A`/`B1B` 是 **ROM/固件代次**,**与频率范围无关**(全系列均 142–1050MHz)。曾误以为"B1B 才是窄带"——**该说法已被官方数据手册证伪**,频率差异来自模块前端而非芯片版本 |
| **能测到 vs 能发射** | 接收灵敏度与发射许可是两件事。一个频段可能"能收不能发"(合规限制) |
| **RSSI vs 信号质量** | RSSI 只是能量,不能区分"有效信号"与"干扰噪声"。必须结合噪底与调制特征 |
| **AI 建议 vs 自动执行** | AI 只给 `recommended_actions[]` 建议;发射必须人工确认(ADR-0002) |

## 4. 缩写表

| 缩写 | 全称 |
|---|---|
| ADR | Architecture Decision Record,架构决策记录 |
| BLE | Bluetooth Low Energy |
| CAPS | Capabilities,设备能力集 |
| HIL | Hardware-in-the-Loop,硬件在环测试 |
| HMAC | Hash-based Message Authentication Code |
| MTU | Maximum Transmission Unit |
| NDJSON | Newline-Delimited JSON |
| OOK/ASK | On-Off Keying / Amplitude Shift Keying |
| FSK | Frequency Shift Keying |
| PWA | Progressive Web App |
| RLE | Run-Length Encoding |
| RMT | Remote Control Transceiver(ESP32 的时序收发外设) |
| RSSI | Received Signal Strength Indicator |
| SRRC | 国家无线电监测中心检测中心(型号核准) |
| WS | WebSocket |
