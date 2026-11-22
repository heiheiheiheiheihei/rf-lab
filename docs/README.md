# RF-Lab 文档索引

| 文档 | 内容 |
|---|---|
| [`00-mvp.md`](00-mvp.md) | ✅ **射频/命令唯一规格**:目标、硬件决策与代价、供电/电平、模块协议、固件行为、射频验收 |
| [`01-ui.md`](01-ui.md) | ✅ **跨平台 UI 规格**:Web/HTTP+SSE、BLE GATT、Android、uConsole TUI、官网同源视觉(§5.1)、交互终端(§5.2)、硬件效果图(§5.3)、码库 v2(§5.4)、一键固件更新规划(§3.4.1)、安全边界与 UI 验收 |
| [`02-plan.md`](02-plan.md) | ✅ **测试债务 + 版本计划**:未完成测试登记(P0/P1/P2)、0.7.2 验证闭合、0.7.3 一键更新、0.8.0 模块抽象、0.9.0 MQTT/HA、1.0 产品化、证据模板 |
| [`03-execution.md`](03-execution.md) | ✅ **执行交接**:锁定决策、当前状态、Phase A/B/C、新会话启动提示、验收命令、禁止事项;状态以 `02-plan.md` 为准 |
| [`evidence/`](evidence/) | 现场证据:串口抓包/波特率判定、0.4.0 首跑、0.5.0 OLED 与长发/停止修正、0.6.0 跨平台 UI、0.7.1 码库 v2、2026-11-22 设备只读巡检、硬件效果图 |
| [`../hardware/README.md`](../hardware/README.md) | 接线 JSON/生成图、引脚、上电检查表、风险 |
| [`../firmware/README.md`](../firmware/README.md) | 构建、烧录、排障、未验证清单 |
| [`../android/README.md`](../android/README.md) | Android APK 构建、权限、BLE 桥、未验证项 |
| [`../uconsole/README.md`](../uconsole/README.md) | uConsole 安装、键位、`Ctrl+Alt+R` 绑定 |
| [`../site/README.md`](../site/README.md) | 官网/配装台:页面结构、价格、交互终端 demo、现役/路线图边界、发布说明 |

`02-plan.md` 是**测试债务与版本计划的唯一所有者**;新增测试项先加到那里,再改组件 README。
`03-execution.md` 只是交接快照,不维护状态;状态以 `02-plan.md` 为准。
其余新文档要么并入上面某一处,要么不加。旧的 `01`–`06`、`protocol/schemas` 已删除(git 历史保留);
`docs/01-ui.md` 是 2026-11-15 新增的 UI 规格,编号不复用旧文档。
