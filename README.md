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

> **范围决策(2026-09-20)**:
> - **放弃 iOS,仅支持 Android**;App 推荐 **PWA**;设备端**增加 I²C OLED**。
> - **2.4GHz 列入本期需求**(M3 保留);产品面向**射频屏蔽室**使用,合规门禁降级为参考设计。
> - **新增 USB 有线控制通道**(F-11/ADR-0003):复用 ESP32-S3 内建 **USB Serial/JTAG 的硬件 CDC-ACM**,**不采用 USB-OTG 外设**(二者共用 GPIO19/20 且互斥;OTG 需外接 PHY 或烧不可逆 eFuse 并失去 JTAG 调试)。用途是**在 2.4GHz 模块发射时保持控制**——屏蔽室挡得住内外互扰,**挡不住同壳体厘米级自扰**,那时无线通道会被自己的 20dBm PA 压掉。
> - 分析留档见 [`docs/40-app/010-mobile-app-design.md`](docs/40-app/010-mobile-app-design.md) §1.5 与 §3.1,USB 决策见 [`docs/adr/0003-usb-wired-control-channel.md`](docs/adr/0003-usb-wired-control-channel.md)。

## 2. 当前状态

| 项 | 状态 |
|---|---|
| 目录骨架 | ✅ 已建立 |
| 设计文档 | ✅ 见 [`docs/`](docs/README.md) — **15 份 Markdown**(overview ×4 / 硬件 / 固件 / 协议 / App / AI / 合规 / 测试 + **3 份 ADR** + 1 份索引),约 35 万字符 |
| 协议契约 | ✅ **v1.1**,已同步机器可读 schema(36 个消息 + common,`$defs` **30** 个),`python3 tools/validate_schemas.py` 退出码 0 |
| 合规文档 | ⚠️ **已降级为参考设计**(屏蔽室用途,非验收门)——但四道门禁、审计、红线仍作为纵深防御保留实现 |
| 测试计划 | ✅ 已成文(**112 个用例**,含 v1.1 边界/负向、A-06 带外可见性、**USB 通道与 A-07**) |
| 固件实现 | ❌ 未开始(本阶段明确不做) |
| App 实现 | ❌ 未开始 |
| 硬件选型定稿 | ✅ **已定稿**(2026-09-20):三块模块即最终配置,**不增补**;范围 = **387–525MHz + 2.4GHz** |

### ✅ 自检命令(纯标准库,无需安装依赖)

```bash
cd rf-lab
python3 tools/validate_schemas.py     # 协议 schema 结构与引用完整性,退出码 0 = 通过
for f in protocol/schemas/*.json; do python3 -m json.tool "$f" >/dev/null || echo "BAD: $f"; done
```

### ✅ 已决策(不再是阻塞项):接受覆盖空白,只支持 387–525MHz + 2.4GHz

需求写的是「119–1050MHz 快速扫描」,但给定的三个模块是**泽耀科技 / Ashining**(厂牌已确认),其**模块级**能力已由**厂商官方手册**核实(2026-09-20):

| 模块 | 芯片 | **模块级**可调频率 | 覆盖 119–1050MHz |
|---|---|---|---|
| AS07-M1101D-SMA | CC1101 | **387–464MHz** | ~8% |
| AS10-M4463D-SMA | SI4463 | **425–525MHz** | ~11% |
| AS01-ML01 | nRF24L01P + RFX2401C | 2400–2525MHz(2.4GHz,已列入需求) | 在 119–1050MHz 之外 |

**结论**:Sub-GHz 模块级真实覆盖仅 **387–525MHz(约占 119–1050MHz 的 15%)**;空白 **119–300 / 525–779 / 928–1050MHz**。**2.4GHz 已于 2026-09-20 列入本期需求**(Q-03 关闭,M3 保留)。

> **2026-09-20 决策(ADR-0001 §5.1):不增补模块,接受覆盖空白。** 硬件已采购完毕,三块模块即最终配置。因此 **119–300 / 525–779 / 928–1050MHz 不是"暂不支持",而是"物理上不可及"**(产品定义 C-08)。
>
> ⚠️ **由此产生一条强制义务**:带外频段必须**"可见地失败"**。用户一定会去试 315MHz / 868MHz(遥控器与传感器的常识频段),而它们恰好全在空白里。设备**禁止**返回空结果让界面显示"该频段干净/无信号" —— 那会把"设备听不见"误报成"没有信号"。已立为成功判据 **A-06** 与零容忍用例 TC-013a / TC-102 / TC-103,详见 [ADR-0001 §5.3](docs/adr/0001-frequency-coverage-gap.md)。

> ⚠️ **这里曾算错过一次,值得记住原因**:此前文档按**芯片级**能力列(CC1101 三段 300–348/387–464/779–928、SI4463 142–1050),得出约 40% 并推断"AS10 搭载窄带 B1B 版芯片"。**两个都错**:① 官方手册显示 **AS07 模块把 CC1101 裁剪到 387–464MHz**、**AS10 模块把 SI4463 裁剪到 425–525MHz**;② 经查 Si446x 数据手册,**C2A/B1B 是 ROM/固件代次,与频率无关**(全系列均 142–1050MHz)。**约束在模块前端,换芯片版本无效** —— 详见 [ADR-0001 §2.1](docs/adr/0001-frequency-coverage-gap.md)。

完整决策与被放弃的选项(留档供日后重评)见 [`docs/adr/0001-frequency-coverage-gap.md`](docs/adr/0001-frequency-coverage-gap.md)。

### ⚠️ 第二条硬约束:433MHz OOK 遥控器只能由 M1 承担

**AS07-M1101D 模块的官方手册"调制"只列 2-FSK / GFSK / MSK,通篇没有 ASK/OOK 字样**(官方原文另称其 2-FSK"与使用 ASK(AM)的简单遥控器不兼容")。**因此 433MHz OOK 遥控器的捕获与复现只能由 M1(SI4463)承担**,成功判据 A-03 已绑定 M1。

> **注意区分层级**:随模块资料包获得的 TI CC1101 数据手册**明确支持 "OOK and flexible ASK shaping"** —— 所以这是**模块级官方口径的缺口**,不是芯片物理限制。若日后需要 AS07 做 OOK,须先向厂商确认固件/寄存器是否放开。

### ✅ 第三条已解决:官方资料已完整取得

此前 `ashining.com.cn` 的 HTML 文档路径返回 403/JS 挑战,但 **`/data/<module>.zip` 直链可用**(50–59MB,需长超时或断点续传)。现已取得三块模块的完整官方资料包(使用手册、注意事项、选型手册、Demo、芯片数据手册),**关闭了全部待核实项**:

- ✅ **V-01**(AS10 可调频率范围 / SI4463 版本)→ 官方手册 **425–525MHz**;原推断证伪
- ✅ **V-09**(AS01-ML01 是否真 20dBm / 含 PA)→ 官方手册标题即写"带 PA、LNA 和屏蔽盖的 2.4G,**100mW**",特征列"采用 RFX2401C 功放芯片,内置 LNA"、"最大功率约 20dBm"
- ❌ **已撤销的推断**:我曾据"18×33.3mm 对应 `AS01-ML01DP5`"提出"可能订错后缀"——**这是错的**。那张表只列**衍生型**;基础型 `AS01-ML01` 本身就是 **18×33.3mm + SMA-K + 20dBm**,与需求逐项一致。用户已确认现货即为 `AS01-ML01`,**无需改单**。见 040 F-4c

> **已关闭(2026-09-20)**:是否有宽覆盖模块 / AS10 能否定制更宽匹配网络 —— **用户已决定不增补、不定制**。方案 B 的候选清单留在 [ADR-0001 §5.2](docs/adr/0001-frequency-coverage-gap.md) 供日后重评,不再作为本期采购输入。

## 3. 目录结构

```
rf-lab/
├── docs/                  设计文档(本阶段主要交付物;含 adr/ 决策记录)
│   ├── 00-overview/       产品目标、场景、术语
│   ├── 10-hardware/       系统架构、多模块共存、天线、BOM
│   ├── 20-firmware/       ESP-IDF 分层、模块驱动模型、RMT 采集
│   ├── 30-protocol/       设备↔App 协议契约(机器可读 schema 见 protocol/schemas/)
│   ├── 40-app/            手机 App(Wi-Fi / BLE / USB 三通道)
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

1. **只支持 387–525MHz,且这是定稿** — 模块级仅覆盖约 15%,空白 119–300 / 525–779 / 928–1050MHz。已决策**接受空白、不增补**(ADR-0001 §5.1)。**由此产生强制义务:带外必须"可见地失败"(A-06)** —— 315/868/915MHz 用户一定会试,禁止让界面显示"无信号"。
2. **433MHz OOK 遥控器只能靠 M1(SI4463)** — AS07 模块官方调制只列 2-FSK/GFSK/MSK(注意:CC1101 **芯片**本身支持 OOK,是模块未承诺)。成功判据 A-03 已绑定 M1。
3. **M3 发射会压掉本机 Wi-Fi/BLE**(20dBm、同频带、同壳体、厘米级间距)—— 后果不止掉线,而是"设备发出的发射动作自己收不到中止指令",属**架构前提级风险**,见 [`docs/10-hardware/010-system-architecture.md`](docs/10-hardware/010-system-architecture.md) §4.2。量级依据(20dBm / RFX2401C PA)**已由官方手册确认**。
4. **仅 Android + PWA**(2026-09-20 决策)—— iOS 的 Web Bluetooth 缺失问题因此不再是设计输入,留档于 [`docs/40-app/`](docs/40-app/010-mobile-app-design.md) §1.5。

### 必须先读 ADR-0001 的原因

[`docs/adr/0001-frequency-coverage-gap.md`](docs/adr/0001-frequency-coverage-gap.md) 记录了本产品**范围上最重要的一次收敛**:需求写 119–1050MHz,而最终硬件只能覆盖 **387–525MHz**,且用户已决定**接受这个空白**。
Q-01 已于 2026-09-20 关闭(厂商官方手册):模块级 **425–525MHz**,且"换 C2A 版芯片就能变宽"是**错的**(C2A/B1B 是 ROM/固件代次,与频率无关)。
**不要按"119–1050MHz 全覆盖"做任何设计**;同时 **不要**把带外频段做成"扫了但没发现" —— 带外必须显式失败(§5.3 / A-06)。

## 5. 合规声明

本项目涉及无线电发射。**任何发射行为都必须遵守使用地的无线电管理规定**,包括但不限于频段许可、发射功率上限、占空比限制与型号核准要求。设备在软件层面强制约束发射频段白名单与功率上限,并留存发射审计日志。详见 [`docs/60-compliance/`](docs/60-compliance/)。

本项目仅供合法的射频学习、自有设备调试与授权测试使用。使用者对自身行为负全部法律责任。

## 6. 开发环境

| 组件 | 本机状态 |
|---|---|
| PlatformIO | 已装,不在 PATH:`~/.platformio/penv/bin/pio` |
| espressif32 平台 | 已下载(`framework-espidf` / `framework-arduinoespressif32` / `toolchain-xtensa-esp-elf`) |
| ESP-IDF 独立安装 | 无 |
| Flutter / Dart | **未安装**(2026-09-20 起 iOS 移出范围,**不再需要**;PWA 走 Node 即可) |
| Node / npm | v22.22.1 / 已装 |
| git | ✅ **本目录已是独立仓库**(2026-09-20 `git init`) |

> **git 身份**:本机全局无 `user.name`/`user.email`,故设置了**仓库级**身份 `RF-Lab Dev <dev@rf-lab.local>`。若要改用真实身份:`git config user.name "…" && git config user.email "…"`。

固件构建(实现阶段启用):

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware && pio run -e esp32s3
```
