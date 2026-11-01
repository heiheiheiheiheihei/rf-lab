# 硬件

| 路径 | 内容 |
|---|---|
| [`../docs/02-hardware.md`](../docs/02-hardware.md) | **硬件设计正文**:开发板事实、引脚分配、模块针序、供电/门控、天线、BOM、接线注意 |
| [`wiring/`](wiring/) | 接线表 + 接线图(SVG/PNG),由 `tools/gen_wiring_diagram.py` **生成**,不要手工编辑 |
| [`wiring/as10-pin-card.png`](wiring/as10-pin-card.png) | AS10 现场认脚卡(由 `tools/gen_as10_pin_card.py` 生成;SMA 端/13.5mm 排距/方焊盘定位) |
| [`bom/`](bom/) | 物料清单(占位;单价待询价) |
| [`datasheets/`](datasheets/) | 厂商官方手册存档,让频率/功率结论可复核 |

> 引脚分配的唯一权威是 [`docs/02-hardware.md`](../docs/02-hardware.md) §3;
> `tools/gen_wiring_diagram.py` 的 `PINS` 是它的可执行镜像。改引脚流程:
> 改 `docs/02-hardware.md` §3 → 改脚本 `PINS` → 重跑 `python3 tools/gen_wiring_diagram.py` → `--check` 逐字节比对生成物。

## 三块模块(模块级能力,来源:厂商官方手册)

| 槽位 | 模块 | 芯片 | 模块级频段 | 功率 | 备注 |
|---|---|---|---|---|---|
| M1 | AS10-M4463D-SMA | Si4463 | 425–525MHz | 20dBm | **433MHz OOK 只能由它承担** |
| M2 | AS07-M1101D-SMA | CC1101 | 387–464MHz | 10dBm | 模块手册未列 OOK(芯片支持,模块未承诺) |
| M3 | AS01-ML01 | nRF24L01P + RFX2401C | 2400–2525MHz | 约 20dBm | 带 PA/LNA/屏蔽罩 |

覆盖空白 119–300 / 525–779 / 928–1050MHz 已接受;带外必须"可见地失败"(A-06)。详见
[`../docs/01-product.md`](../docs/01-product.md) §4。

## 官方资料存档(`datasheets/`)

厂商官网的 HTML 文档路径对部分环境返回 403 / JS 挑战,而 `/data/<module>.zip` 直链可用但每个 50–59MB。
为让频率/功率结论可复核,这里存了**去重后的关键文档子集**(约 9MB):

| 文件 | 内容 | 支撑的结论 |
|---|---|---|
| `AS10-M4463D-SMA_官方手册_2025-12.pdf` | 泽耀官方使用手册 | M1 的 425–525MHz、20dBm、−126dBm |
| `AS07-M1101D-SMA_官方手册.pdf` | 泽耀官方使用手册 | M2 的 387–464MHz、10dBm、−111dBm、调制只列 2-FSK/GFSK/MSK |
| `AS01-ML01_官方手册.pdf` | 泽耀官方使用手册 | M3 的 2.4–2.525GHz、约 20dBm、RFX2401C PA/LNA |
| `泽耀科技产品选型手册.pdf` | 厂牌全系列选型 | 型号后缀对照 |
| `Si446x_数据手册_Rev1.4.pdf` | Silicon Labs 原厂数据手册 | Si4463 芯片 142–1050MHz;`Bxx` 是 ROM/固件代次(与频率无关) |
| `CC1101_数据手册_extracted.txt` | TI CC1101 原厂手册文本抽取 | 芯片支持 OOK/ASK(与 AS07 模块未列出 OOK 形成对照) |

> 三个完整资料包(含 Demo、WDS、开发板资料,约 160MB)不入库;需要时按官方直链
> `http://www.ashining.com.cn/data/<module>.zip` 用 `curl -C -` 断点续传下载。
