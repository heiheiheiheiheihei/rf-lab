# RF-Lab · 便携式多频段射频实验平台

一款由 Android 手机 App 经 **USB 有线通道**控制、面向**射频屏蔽室**场景的多频段无线收发实验平台。
设备本体是 ESP32-S3 + 三块独立射频模块(Sub-GHz ×2 + 2.4GHz),每个模块独占 SPI 与天线;
AI 分析在 App 侧调用 DeepSeek,设备只回传特征。

## 当前状态(2026-10-31)

| 项 | 状态 |
|---|---|
| 固件阶段 1 骨架 | ✅ 可烧录、可通信:`HELLO` / `GET_CAPS` / `PING` 经 CH343P **@921600**;2026-10-31 已修复帧发送(ROM putc 改写二进制)、CC1101 状态寄存器读头、POR 时序、门控/配对/帧超时等审计问题 |
| 射频探测 | 🔶 固件缺陷已修(原 M2 `partnum=0xFF` 由固件读头错误导致);接线改为 **M1 SDN=GPIO9(开漏+100k 上拉)**、**M3 CE=AND(GPIO7,GPIO8)**;待按新接线重烧后复测 |
| 协议契约 | ✅ v1.1,机器可读 schema + fixtures 求值通过(`python3 tools/validate_schemas.py`) |
| App | ❌ 未开始 |
| 文档 | ✅ 2026-10-25 由 19 份缩编为 **6 份**,入口 [docs/README.md](docs/README.md);2026-10-31 接线/固件审计修复已同步 |

## 硬件配置

| 槽位 | 模块 | 芯片 | 模块级频段 | 功率 |
|---|---|---|---|---|
| M1 | AS10-M4463D-SMA | Si4463 | 425–525MHz | 20dBm |
| M2 | AS07-M1101D-SMA | CC1101 | 387–464MHz | 10dBm(硬件上限) |
| M3 | AS01-ML01 | nRF24L01P + RFX2401C | 2400–2525MHz | 20dBm |

- 主控:ESP32-S3-WROOM-1 **N16R8**,开发板 VCC-GND **YD-ESP32-S3**(`HW678`)。
- **控制通道 = 板载 CH343P(VID:PID `1a86:55d3`)→ UART0 @921600**;另一个 Type-C 口的原生 USB 在本板从未枚举成功。
- **M3 门控**:`CE = AND(GPIO7 2.4G总闸, GPIO8 RF_KILL)`;KILL 低 ⇒ CE=0 ⇒ nRF24 power-down/PA 不工作(仍可由 GPIO6 上电做 SPI 只读探测)。
- **M1 SDN = GPIO9**(开漏 + 外部 100kΩ 上拉 3.3V;固件上电 POR 脉冲:释放高 ≥100µs → 拉低 → 等 ≥10ms)。
- 覆盖空白 119–300 / 525–779 / 928–1050MHz 已接受;带外必须"可见地失败"(A-06)。

## 快速开始

```bash
# 构建 + 烧录(经 CH343P)
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd firmware
pio run -e esp32s3
pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0

# 上电自检:HELLO / GET_CAPS / PING(921600)
cd ..
python3 tools/bringup_test.py --port /dev/ttyACM0

# 协议 schema 校验(结构 + fixtures 载荷求值)
python3 tools/validate_schemas.py

# 接线交付物自检(预算/几何/生成物逐字节一致)
python3 tools/gen_wiring_diagram.py --check
python3 tools/gen_as10_pin_card.py --check
```

> 波特率注意:App 横幅与协议帧都是 **921600**;复位后 ROM bootloader 自己的几行是 **115200**,
> 用 921600 打开会先看到一小段乱码,属正常。

## 仓库结构

```
rf-lab/
├── docs/                  设计文档(6 份,见 docs/README.md)
├── protocol/schemas/      协议 JSON Schema(机器可读镜像)
├── firmware/              ESP-IDF 工程(构建/烧录速查见 firmware/README.md)
├── app/                   手机 App(占位,本批不实现)
├── hardware/              BOM / 接线图(生成物)/ 厂商手册存档
└── tools/                 接线图生成器 / schema 校验 / 上电自检
```

## 文档入口

- [docs/README.md](docs/README.md) — 索引与权威关系
- [01-product.md](docs/01-product.md) — 产品、范围、需求/约束/成功判据、关键决策
- [02-hardware.md](docs/02-hardware.md) — 硬件、引脚与模块针序、供电/门控、接线注意
- [03-firmware.md](docs/03-firmware.md) — 固件现状、构建/烧录、探测实现、已知缺口
- [04-protocol.md](docs/04-protocol.md) — 协议契约(**唯一权威**)
- [05-app-ai.md](docs/05-app-ai.md) — App 设计与 DeepSeek 分析链路
- [06-test-plan.md](docs/06-test-plan.md) — 测试用例、验收判据、当前可执行测试与联调记录

## 安全与合规

- 本产品按**射频屏蔽室内使用**设计;四道发射门禁 + 审计日志仍作为纵深防御实现。
- 任何发射必须经用户显式确认 + 短时 HMAC 令牌;出厂 `tx_whitelist` 为空 = 禁止一切发射。
- 本项目仅供合法的射频学习、自有设备调试与授权测试使用;使用者对自身行为负全部法律责任。
