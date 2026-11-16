# RF-Lab 文档索引

| 文档 | 内容 |
|---|---|
| [`00-mvp.md`](00-mvp.md) | ✅ **射频/命令唯一规格**:目标、硬件决策与代价、供电/电平、模块协议、固件行为、射频验收 |
| [`01-ui.md`](01-ui.md) | ✅ **跨平台 UI 规格**:Web/HTTP+SSE、BLE GATT、Android、uConsole TUI、安全边界与 UI 验收 |
| [`evidence/`](evidence/) | 现场证据:串口抓包/波特率判定、0.4.0 首跑、0.5.0 OLED 与长发/停止修正、0.6.0 跨平台 UI |
| [`../hardware/README.md`](../hardware/README.md) | 接线、引脚权威、上电检查表、风险 |
| [`../firmware/README.md`](../firmware/README.md) | 构建、烧录、排障、未验证清单 |
| [`../android/README.md`](../android/README.md) | Android APK 构建、权限、BLE 桥、未验证项 |
| [`../uconsole/README.md`](../uconsole/README.md) | uConsole 安装、键位、`Ctrl+Alt+R` 绑定 |
| [`../site/README.md`](../site/README.md) | 官网/配装台:页面结构、价格、现役/路线图边界、发布说明 |

新文档要么并入上面某一处,要么不加。旧的 `01`–`06`、`protocol/schemas` 已删除(git 历史保留);
`docs/01-ui.md` 是 2026-11-15 新增的 UI 规格,编号不复用旧文档。
