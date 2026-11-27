# RF-Lab 文档索引

| 文档 | 内容 |
|---|---|
| [`00-mvp.md`](00-mvp.md) | ✅ **射频/命令唯一规格**:目标、硬件决策与代价、供电/电平、模块协议、固件行为、射频验收 |
| [`01-ui.md`](01-ui.md) | ✅ **跨平台 UI 规格**:Web/HTTP+SSE、BLE GATT、Android、uConsole TUI、官网同源视觉(§5.1)、交互终端(§5.2)、硬件效果图(§5.3)、码库 v2(§5.4)、一键固件更新规划(§3.4.1)、安全边界与 UI 验收 |
| [`02-plan.md`](02-plan.md) | ✅ **测试债务 + 版本计划**:未完成测试登记(P0/P1/P2)、0.7.2 验证收尾、0.7.3 一键更新、0.8.0 模块抽象、0.9.0 MQTT/HA、1.0 产品化、证据模板 |
| [`03-execution.md`](03-execution.md) | ✅ **薄交接页**:锁定决策、当前队列、新会话启动提示、禁止事项;不维护状态,状态以 `02-plan.md` 为准 |
| [`04-ref-mynova.md`](04-ref-mynova.md) | 📎 **外部参考台账**:MYNOVA RFC V2 的 CC1101/RAW/.sub/HA/打包/测试 seam 可复用事实与许可边界;**不是规格也不是计划**,冲突以 00/01/02 为准 |
| [`adr/`](adr/README.md) | 🧭 **架构决策记录**:ADR-001 射频 HAL/CC1101 接入边界(干净室规则)、ADR-003 码库/RAW 存储;每篇只回答「为什么这样定」,状态与索引见 [`adr/README.md`](adr/README.md) |
| [`evidence/`](evidence/) | 现场证据:串口/波特率、0.4–0.6 上机、0.7.1 码库、0.7.2 Release/设备 OTA、BLE/手表、2026-11-24 radio HAL;索引见 [`evidence/README.md`](evidence/README.md) |
| [`../hardware/README.md`](../hardware/README.md) | 接线 JSON/生成图、引脚、上电检查表、风险 |
| [`../firmware/README.md`](../firmware/README.md) | 构建、烧录、排障、未验证清单 |
| [`../android/README.md`](../android/README.md) | Android APK 构建、权限、BLE 桥、未验证项 |
| [`../uconsole/README.md`](../uconsole/README.md) | uConsole 安装、键位、`Ctrl+Alt+R` 绑定 |
| [`../site/README.md`](../site/README.md) | 官网/配装台:页面结构、价格、交互终端 demo、现役/路线图边界、发布说明 |

`02-plan.md` 是**测试债务与版本计划的唯一所有者**;新增测试项先加到那里,再改组件 README。
`03-execution.md` 只是薄交接页,不维护状态。
其余新文档要么并入上面某一处,要么不加。`04-ref-mynova.md` 与 `adr/` 单独登记,是因为
「外部参考」与「架构决策」没有别的所有者:前者只记录上游事实与复用判断,后者只回答「为什么这样定」,
都不复述规则。**证据只增不改**:新结论写新文件,旧文件保留原值。
