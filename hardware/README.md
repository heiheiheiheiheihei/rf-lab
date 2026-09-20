# 硬件(占位)

- 系统架构设计见 [`../docs/10-hardware/010-system-architecture.md`](../docs/10-hardware/010-system-architecture.md)
- 频段覆盖空白分析见 [`../docs/adr/0001-frequency-coverage-gap.md`](../docs/adr/0001-frequency-coverage-gap.md)
- BOM 表见 [`bom/`](bom/)

## 已核实的模块频率能力

| 槽位 | 模块 | 芯片 | 实际频率能力 | 标称功率 |
|---|---|---|---|---|
| 1 | AS10-M4463D-SMA | SI4463 | C2A 142–1050MHz / B1B 425–525MHz(**待向供应商确认是哪版**) | 20dBm |
| 2 | AS07-M1101D-SMA | CC1101 | 300–348 / 387–464 / 779–928MHz | 10dBm |
| 3 | AS01-ML01 | nRF24L01P + RFX2401C | 2400–2525MHz | 20dBm |

⚠️ 上述三模块**无法覆盖**需求的 119–1050MHz,空白区见 ADR-0001。
