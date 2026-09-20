# 文档索引

状态:`draft` · 最后更新:2026-09-20

> **本目录是项目的设计权威。** 代码实现必须服从这里定义的契约;若实现需要偏离,先改文档(并记录理由),再改代码。

## 阅读路线

### 我是新加入的开发者

1. [`../README.md`](../README.md) — 项目是什么、当前状态、**已知阻塞项**
2. `00-overview/010-product-definition.md` — 需求与范围
3. `00-overview/030-glossary.md` — **先看术语**,避免理解偏差
4. `00-overview/020-architecture.md` — 三层全景与职责边界
5. `00-overview/040-verified-platform-facts.md` — 承重事实与**复核触发条件**
6. `30-protocol/010-device-app-protocol.md` — 契约(改动前必读)

### 我要改什么

- **改硬件** → `adr/0001-frequency-coverage-gap.md` → `10-hardware/010-system-architecture.md` → `hardware/bom/`
- **改协议** → `30-protocol/010-device-app-protocol.md`(权威)**v1.1** → `protocol/schemas/`(机器可读镜像)→ 跑 `python3 tools/validate_schemas.py` → 同步通知固件与 App 两侧
- **改发射相关行为** → **必须**先读 `60-compliance/010-spectrum-compliance.md`(四道发射门、红线)与 `adr/0002-ai-analysis-in-app.md` §4.3,以及契约 §7.1(令牌定义)

> **协议改动自检(强制)**:任何字段改动必须在**同一批**里改完 `30-protocol/` + `protocol/schemas/`,并跑通 `python3 tools/validate_schemas.py`(退出码 0)。schema 目录的 README §5 有完整清单。

## 文档清单

| 路径 | 主题 | 状态 |
|---|---|---|
| `00-overview/010-product-definition.md` | 产品定义、需求 F-01..F-10、约束、成功判据 | draft |
| `00-overview/020-architecture.md` | 顶层三层架构、决策 D1–D3、数据流、职责边界 | draft |
| `00-overview/030-glossary.md` | 术语表与易混淆对 | draft |
| `00-overview/040-verified-platform-facts.md` | **已核实的平台事实**(DeepSeek 模型名、Web Bluetooth、模块频率、本机环境) | verified |
| `10-hardware/010-system-architecture.md` | 硬件框图、引脚分配、多 SPI 方案、共存干扰、天线、供电、BOM | draft |
| `20-firmware/010-firmware-architecture.md` | 固件分层、驱动接口、RMT 捕获、扫频、任务模型、可测性 | draft |
| `30-protocol/010-device-app-protocol.md` | **协议契约**(帧格式、消息、能力集、状态机、错误码、安全) | **draft · v1.1** |
| `40-app/010-mobile-app-design.md` | App 载体选型、双通道、UI 架构、交互时序、数据模型 | draft |
| `50-ai/010-deepseek-analysis.md` | DeepSeek 调用链、提示词契约、结构化输出、威胁判据 | draft |
| `60-compliance/010-spectrum-compliance.md` | 法规依据、频段可用性矩阵、软件强制约束、免责声明 | draft |
| `70-test/010-verification-plan.md` | 测试层次、用例表、验收步骤、合规负向测试 | draft |
| `adr/0001-frequency-coverage-gap.md` | **119–1050MHz 覆盖空白**与处置方案 | proposed |
| `adr/0002-ai-analysis-in-app.md` | AI 分析放 App 侧的决策与理由 | accepted |

## 状态定义

| 状态 | 含义 |
|---|---|
| `draft` | 已成文,可评审,可能有待决问题 |
| `proposed` | 决策待用户批准 |
| `accepted` | 已批准,实现必须遵守 |
| `superseded` | 已被更新的文档取代,保留供追溯 |
| `编写中` | 尚未完成 |

## 待决问题汇总

| ID | 问题 | 出处 | 阻塞什么 |
|---|---|---|---|
| Q-01 | AS10-M4463D-SMA 搭载的 SI4463 是 C2A(142–1050MHz)还是 B1B(425–525MHz)? | ADR-0001 | 决定频段覆盖策略与 BOM |
| Q-02 | 是否接受新增宽覆盖模块以补齐空白? | ADR-0001 | BOM 与成本 |
| Q-03 | 119–142MHz 是否必须覆盖? | ADR-0001 | 天线与合规双重困难 |
| Q-04 | 2.4GHz 是否列入本期需求? | 产品定义 §7 | 槽位 3 是否保留 |
| Q-05 | 手机 App 载体:PWA 还是原生?(受 **iOS 不支持 Web Bluetooth** 影响) | `40-app/` §1 | App 技术栈与 iOS 覆盖 |
| Q-06 | 目标使用地(大陆 / 出口)? | `60-compliance/` | 适用法规与 `CAPS.regulatory_region` |
| **Q-07** | **发射授权路径**:只走 BLE 配对,还是接受"设备端本地授权"或"带外传密钥"? | 契约 §7.1(5) | **iOS 纯 Wi-Fi 设备永久无法发射**;决定是否需改硬件 |
| **Q-08** | 设备端是否需要**显示屏**(`pair_code` 的显示载体)? | 契约 §7.1(1) | 首次配对流程能否成立;BOM |
| **Q-09** | 是否要求"设备侧离线记录→回读"能力? | 契约 §9.1 | PWA 前后台必断 → S1 长时无人值守能否成立 |

> **Q-05 与 Q-07 是同一个约束的两面**:iOS 全平台不支持 Web Bluetooth([caniuse](https://caniuse.com/web-bluetooth),根因是 [App Store 审核指南 2.5.6](https://developer.apple.com/app-store/review/guidelines/) 强制 iOS 浏览器使用 WebKit)。而 `pairing_secret` 只经 BLE 下发 ⇒ **纯 PWA / 纯 Wi-Fi 路径在 iPhone 上连"绑定"都做不到,遑论发射**。详见 App 设计文档 §1 与契约 §7.1(5)。

## 文档约定

- 每份文档开头必须有:`状态 · 最后更新 · 上游依据`
- 区分**已核实事实**(带来源 URL)与**建议/推算**,后者必须标注
- 不确定的合规结论写"需专业确认",**绝不编造法规限值**
- 协议字段的权威来源只有一个:`30-protocol/010-device-app-protocol.md`
- 决策不写在散落段落里,写进 `adr/`
