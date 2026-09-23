# 硬件(占位)

- 系统架构设计见 [`../docs/10-hardware/010-system-architecture.md`](../docs/10-hardware/010-system-architecture.md)
- **接线图见 [`wiring/`](wiring/)(SVG + 引脚表,由 `tools/gen_wiring_diagram.py` 生成)**
- 频段覆盖空白分析见 [`../docs/adr/0001-frequency-coverage-gap.md`](../docs/adr/0001-frequency-coverage-gap.md)
- BOM 表见 [`bom/`](bom/)

## 已核实的模块能力(**来源:厂商官方手册**)

数据来源:泽耀科技(Ashining)官方模块资料包,2026-09-20 取得。

| 槽位 | 模块 | 芯片 | **模块级**频率(软件可调) | 发射功率 | 灵敏度 | 官方链接 |
|---|---|---|---|---|---|---|
| 1 | AS10-M4463D-SMA | SI4463 | **425–525MHz** | 20dBm / 100mW | −126dBm @1kbps | [资料包](http://www.ashining.com.cn/data/as10_m4463d_sma.zip) |
| 2 | AS07-M1101D-SMA | CC1101 | **387–464MHz**(推荐 433±10MHz) | 10dBm / 10mW | −111dBm @1.2kbps | [资料包](http://www.ashining.com.cn/data/as07_m1101d_sma.zip) |
| 3 | AS01-ML01 | nRF24L01P + RFX2401C | 2400–2525MHz(126 信道) | 20dBm / 100mW | 内建 LNA | [资料包](http://www.ashining.com.cn/data/as01_ml01.zip) |

**芯片级能力(≠ 模块能力!)**:SI4463 芯片 **142–1050MHz**([数据手册](https://www.silabs.com/documents/public/data-sheets/Si4464-63-61-60.pdf));CC1101 芯片 **300–348 / 387–464 / 779–928MHz**。
**两块模块的前端都把芯片能力砍掉了**:AS10 从 142–1050 砍到 425–525,AS07 从三段砍到 387–464。

### 调制方式与功能归属(官方手册口径)

| 模块 | 官方列出的调制 | 能否收发 433MHz OOK 遥控? |
|---|---|---|
| AS10-M4463D-SMA | SI4463 支持 OOK/ASK 与 FSK 族 | ✅ **是**(M1 独占) |
| AS07-M1101D-SMA | 官方手册**只列 2-FSK / GFSK / MSK**,无 OOK/ASK | ❌ 官方口径不支持 |
| AS01-ML01 | GFSK | 频段在 2.4GHz,与 433MHz 无关 |

> ⚠️ **注意区分**:CC1101 **芯片**数据手册第 94 行明确支持 "OOK and flexible ASK shaping",但 **AS07 模块官方手册通篇没有 ASK/OOK 字样**。因此"AS07 不能收发 OOK"是**模块级官方口径的缺口**,不是芯片物理限制。若日后需要 AS07 做 OOK,须先向厂商确认固件/寄存器是否放开。

⚠️ **三模块合计无法覆盖需求的 119–1050MHz**:模块级真实覆盖仅 387–525MHz(约 15%),空白 119–300 / 525–779 / 928–1050MHz。详见 [ADR-0001](../docs/adr/0001-frequency-coverage-gap.md)。

---

## 官方资料存档(`datasheets/`)

厂商官网(`ashining.com.cn`)的 HTML 文档路径对本环境返回 403 / JS 挑战,而 `/data/<module>.zip` 直链可用但每个 50–59MB。为让上表的每一个数字**日后可复核**,这里存了**去重后的关键文档子集**(9MB,而非三个完整资料包的 160MB):

| 文件 | 内容 | 支撑哪些结论 |
|---|---|---|
| `AS10-M4463D-SMA_官方手册_2025-12.pdf` | 泽耀官方使用手册(v2025-12) | 槽位 1 的 **425–525MHz**、20dBm、−126dBm |
| `AS07-M1101D-SMA_官方手册.pdf` | 泽耀官方使用手册 | 槽位 2 的 **387–464MHz**、10dBm、−111dBm、调制只列 2-FSK/GFSK/MSK |
| `AS01-ML01_官方手册.pdf` | 泽耀官方使用手册 | 槽位 3 的 2.4–2.525GHz、**约 20dBm**、RFX2401C PA/LNA、18×33.3mm |
| `泽耀科技产品选型手册.pdf` | 厂牌全系列选型 | 型号后缀对照(AS01-ML01DP3/DP5/DP6…) |
| `Si446x_数据手册_Rev1.4.pdf` | Silicon Labs 原厂数据手册 | **Si4463 全系列 142–1050MHz**;`Bxx` = ROM+固件代次(证伪"C2A/B1B 决定频率") |
| `CC1101_数据手册_extracted.txt` | TI CC1101 原厂手册文本抽取 | 芯片支持 **OOK/ASK**(与 AS07 模块未列出 OOK 形成对照) |

> **未存档**:三个完整资料包(含 Demo 程序、WDS 配置器、开发板资料,共约 160MB)体积过大、且属可重新下载的二进制,不入库。
> 需要时重新下载(直链可用,zip 较大,建议 `curl -C -` 断点续传):
> ```bash
> for m in as10_m4463d_sma as07_m1101d_sma as01_ml01; do
>   curl -SL -C - -o "$m.zip" "http://www.ashining.com.cn/data/$m.zip"
> done
> ```
> **若希望仓库不含二进制**,删除本目录并 `git rm -r hardware/datasheets` 即可——文档中的结论不依赖这些文件的物理存在,只依赖其被核对过。
