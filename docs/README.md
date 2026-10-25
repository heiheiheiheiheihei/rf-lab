# RF-Lab 文档索引

> **2026-10-25 文档缩编**:原 19 份设计文档合并为下面 6 份。正文只保留当前有效的事实与规范;
> 修订历史、被否决方案的长篇留档、已关闭问题不再保留(需要追溯时看 git 历史)。

| 文档 | 内容 |
|---|---|
| [01-product.md](01-product.md) | 产品定义、使用场景、本批范围、需求/约束/成功判据、关键决策、术语 |
| [02-hardware.md](02-hardware.md) | 硬件架构、开发板事实、引脚与模块针序、供电/门控、天线、BOM、接线注意 |
| [03-firmware.md](03-firmware.md) | 固件现状、源码结构、控制通道、启动流程、射频探测、构建/烧录、目标架构、已知缺口 |
| [04-protocol.md](04-protocol.md) | 设备 ↔ App 协议契约(**唯一权威**;机器可读镜像见 `protocol/schemas/`) |
| [05-app-ai.md](05-app-ai.md) | App 载体/连接/配对/UI,DeepSeek 分析链路与提示词契约 |
| [06-test-plan.md](06-test-plan.md) | 验证层次、测试用例、验收判据、阶段门禁、当前可执行测试与联调记录 |

## 辅助目录(不是设计文档)

| 路径 | 说明 |
|---|---|
| `protocol/schemas/` | 契约的机器可读镜像;`python3 tools/validate_schemas.py` 校验(退出码 0 = 通过) |
| `hardware/wiring/` | 接线表 + 接线图,由 `tools/gen_wiring_diagram.py` **生成**;引脚单一数据源是该脚本的 `PINS` |
| `firmware/README.md` | 构建 / 烧录 / 自检速查 |
| `hardware/README.md`、`app/README.md` | 目录速查 |

## 权威关系

- **协议字段**:以 [04-protocol.md](04-protocol.md) 为唯一权威。改字段必须**同一批**改完 `protocol/schemas/`,
  并跑通 `python3 tools/validate_schemas.py`。
- **引脚分配**:以 `tools/gen_wiring_diagram.py` 的 `PINS` 为唯一数据源。改引脚流程:
  改 `docs/02-hardware.md` §3 → 改脚本 `PINS` → 重跑 `python3 tools/gen_wiring_diagram.py`。
- **已核实 / 待核实**:正文中带"待核实"的条目在实现前必须补齐;不要把推断当事实。

## 自检命令(纯标准库)

```bash
cd rf-lab
python3 tools/validate_schemas.py           # 协议 schema 结构与引用完整性
python3 tools/gen_wiring_diagram.py --check # 引脚预算 + 生成器几何自检
```
