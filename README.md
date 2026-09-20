# RF-Lab · 便携式多频段射频实验平台

> 一款由手机 App 灵活控制的、支持多频段无线收发的、可接入 DeepSeek 进行智能分析的便携式射频工具。

本仓库当前阶段:**设计阶段(骨架 + 设计文档),不含固件实现。**
固件框架已定为 **ESP-IDF**(ESP32-S3N16R8);AI 分析固定在 **手机 App 侧**调用 DeepSeek,设备只负责回传原始信号特征。

---

## 1. 这是什么

一个免焊接、可远程控制的无线射频实验平台。设备本体(ESP32-S3 + 多个独立射频模块,每个模块独占 SPI 与天线)放在车内、背包等远处;人通过**手机 App(仅 Android)**经 Wi-Fi(路由器)或 BLE(近距离)遥控它完成:

- **自动扫频** — 在目标频段快速扫描,标记 RSSI 超阈值的信号
- **信号解码** — 捕获 OOK/FSK 原始时序,识别遥控器、传感器等协议
- **信号发射** — 在合规频段与功率下复现/模拟目标信号
- **智能分析** — App 把信号特征交给 DeepSeek,拿回信号类型 / 协议 / 威胁等级

设备端带 **OLED 显示屏**(显示配对码、AP 地址、模块与门控状态)——在"人不在设备旁边"时,它是唯一的本地状态出口。

> **范围决策(2026-09-20)**:**放弃 iOS,仅支持 Android**;App 推荐 **PWA**;设备端**增加 I²C OLED**。分析留档见 [`docs/40-app/010-mobile-app-design.md`](docs/40-app/010-mobile-app-design.md) §1.5 与 §3.1。

## 2. 当前状态

| 项 | 状态 |
|---|---|
| 目录骨架 | ✅ 已建立 |
| 设计文档 | ✅ 见 [`docs/`](docs/README.md) — **14 份 Markdown**(overview ×4 / 硬件 / 固件 / 协议 / App / AI / 合规 / 测试 + 2 份 ADR + 1 份索引),约 32 万字符 |
| 协议契约 | ✅ **v1.1**,已同步机器可读 schema(36 个消息 + common,`$defs` 29 个),`python3 tools/validate_schemas.py` 退出码 0 |
| 合规文档 | ✅ 已成文(四道发射门、红线、审计必录字段) |
| 测试计划 | ✅ 已成文(120+ 个用例,含 v1.1 新增的边界与负向测试;用例数随合规文档收尾仍在增加) |
| 固件实现 | ❌ 未开始(本阶段明确不做) |
| App 实现 | ❌ 未开始 |
| 硬件选型定稿 | ⚠️ **有阻塞项**,见下 |

### ✅ 自检命令(纯标准库,无需安装依赖)

```bash
cd rf-lab
python3 tools/validate_schemas.py     # 协议 schema 结构与引用完整性,退出码 0 = 通过
for f in protocol/schemas/*.json; do python3 -m json.tool "$f" >/dev/null || echo "BAD: $f"; done
```

### ⚠️ 阻塞项:现有三个模块拼不出 119–1050MHz

需求写的是「119–1050MHz 快速扫描」,但给定的三个模块是**泽耀科技 / Ashining**(厂牌已确认,官方文档入口见 [`040-verified-platform-facts.md`](docs/00-overview/040-verified-platform-facts.md) F-4b),覆盖如下:

| 模块 | 芯片 | 实际频率能力 | 覆盖 119–1050MHz |
|---|---|---|---|
| AS07-M1101D-SMA | CC1101 | **387–464MHz**(官方规格;芯片本身为 300–348 / 387–464 / 779–928 三段) | ~9% |
| AS10-M4463D-SMA | SI4463 | 官方口径 **433MHz**;可调范围待规格书(V-01)。C2A 版芯片为 142–1050MHz / B1B 版为 425–525MHz | **待确认** |
| AS01-ML01 | nRF24L01P | 2400–2525MHz | 在目标范围之外 |

**结论**:119–142 / 348–387 / 525–779 / 928–1050MHz 存在覆盖空白;2.4GHz 需单列需求。
完整分析与处置建议见 [`docs/adr/0001-frequency-coverage-gap.md`](docs/adr/0001-frequency-coverage-gap.md)。

### ⚠️ 第二条硬约束:M2/M3 不能解 OOK

官方规格明确 **AS07-M1101D(CC1101)不支持 OOK/ASK**——其 2-FSK 基于频率调制,原文写明"与使用 ASK(AM)的简单遥控器不兼容"。**因此 433MHz OOK 遥控器的捕获与复现只能由 M1(SI4463)承担**,成功判据 A-03 已绑定 M1。详见[产品定义 §6.2](docs/00-overview/010-product-definition.md)。

### ⚠️ 第三条:一次网络受限,有 2 项官方参数没拿到

`ashining.com.cn` 与 `sekorm.com` 的深层文档路径对本环境返回 403 / JS 挑战 / 人机验证,**官方 PDF 正文未取得**。因此:
- **V-01**:AS10-M4463D-SMA 的**可调频率范围**与**对应 SI4463 版本**(仍是 Q-01,决定 60% 覆盖)
- **V-09**:**AS01-ML01 的实际发射功率与是否含 PA**(官方带 PA 的是 `AS01-ML01DP3`;而"20dBm"正是"发射压掉本机 Wi-Fi"风险的量级依据)

**这两项必须向供应商索取规格书正文关闭**,不要用推断代替。

## 3. 目录结构

```
rf-lab/
├── docs/                  设计文档(本阶段主要交付物)
│   ├── 00-overview/       产品目标、场景、术语
│   ├── 10-hardware/       系统架构、多模块共存、天线、BOM
│   ├── 20-firmware/       ESP-IDF 分层、模块驱动模型、RMT 采集
│   ├── 30-protocol/       设备↔App 协议契约(JSON Schema)
│   ├── 40-app/            手机 App(Wi-Fi / BLE 双通道)
│   ├── 50-ai/             DeepSeek 分析链路与提示词契约
│   ├── 60-compliance/     频段合规、发射授权、审计
│   ├── 70-test/           验证策略与验收标准
│   └── adr/               架构决策记录
├── protocol/schemas/      机器可读的协议 JSON Schema
├── firmware/              ESP-IDF 工程位置(仅占位,未实现)
├── app/                   手机 App 位置(仅占位)
├── hardware/bom/          物料清单
└── tools/                 辅助脚本
```

## 4. 阅读顺序

> **本阶段的交付物就是文档。** 入口是 [`docs/README.md`](docs/README.md)(索引 + 每份文档状态 + 待决问题汇总)。

1. [`docs/README.md`](docs/README.md) — 文档索引与状态
2. [`docs/00-overview/010-product-definition.md`](docs/00-overview/010-product-definition.md) — 产品定义与范围
3. [`docs/00-overview/020-architecture.md`](docs/00-overview/020-architecture.md) — 顶层架构
4. [`docs/00-overview/040-verified-platform-facts.md`](docs/00-overview/040-verified-platform-facts.md) — **已核实事实与复核触发条件**(含"哪些结论只是待办")
5. [`docs/30-protocol/010-device-app-protocol.md`](docs/30-protocol/010-device-app-protocol.md) — 协议契约 **v1.1**(改动前必读)
6. [`protocol/schemas/README.md`](protocol/schemas/README.md) — 机器可读 schema 的清单、约定与「已知文档缺口」
7. [`docs/adr/`](docs/adr/) — 已定决策与其理由(0001 频段空白 / 0002 AI 放 App 侧)

### 会改变设计的事实(别在读完前动手)

1. **给定三模块拼不出 119–1050MHz** — 见 ADR-0001 与上文阻塞项。
2. **M2/M3 不能解 OOK** — 433MHz OOK 遥控器只能靠 M1(SI4463),成功判据 A-03 已绑定 M1。
3. **M3 发射会压掉本机 Wi-Fi/BLE**(20dBm、同频带、同壳体、厘米级间距)—— 后果不止掉线,而是"设备发出的发射动作自己收不到中止指令",属**架构前提级风险**,见 [`docs/10-hardware/010-system-architecture.md`](docs/10-hardware/010-system-architecture.md) §4.2。⚠️ 该风险的量级依据(20dBm)尚未核实,见 V-09。
4. **仅 Android + PWA**(2026-09-20 决策)—— iOS 的 Web Bluetooth 缺失问题因此不再是设计输入,留档于 [`docs/40-app/`](docs/40-app/010-mobile-app-design.md) §1.5。

### 必须先读 ADR-0001 的原因

[`docs/adr/0001-frequency-coverage-gap.md`](docs/adr/0001-frequency-coverage-gap.md) 说明了一个会影响整个产品定义的问题:**给定模块无法覆盖需求的 119–1050MHz**。
在确认 Q-01(模块搭载的 SI4463 是哪一版)之前,不要按"119–1050MHz 全覆盖"做任何设计。

## 5. 合规声明

本项目涉及无线电发射。**任何发射行为都必须遵守使用地的无线电管理规定**,包括但不限于频段许可、发射功率上限、占空比限制与型号核准要求。设备在软件层面强制约束发射频段白名单与功率上限,并留存发射审计日志。详见 [`docs/60-compliance/`](docs/60-compliance/)。

本项目仅供合法的射频学习、自有设备调试与授权测试使用。使用者对自身行为负全部法律责任。

## 6. 开发环境

| 组件 | 本机状态 |
|---|---|
| PlatformIO | 已装,不在 PATH:`~/.platformio/penv/bin/pio` |
| espressif32 平台 | 已下载(`framework-espidf` / `framework-arduinoespressif32` / `toolchain-xtensa-esp-elf`) |
| ESP-IDF 独立安装 | 无 |
| Flutter / Dart | **未安装**(App 若选原生框架需先装) |
| Node / npm | v22.22.1 / 已装 |

固件构建(实现阶段启用):

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware && pio run -e esp32s3
```
