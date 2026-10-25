/*
 * rf_spi.h — 三模块共享的 SPI2 总线(硬件文档 §3.2 / 接线文档)
 *
 * 事实来源:
 *   docs/02-hardware.md §3(引脚表)
 *   hardware/wiring/README.md(共享 SPI2 总线)
 *
 * ⚠️ 为什么 CS 用软件(不是硬件 CS):
 *   射频侧是**一条总线 + 3 个片选**。用硬件 CS 需要每个设备一份 spi_device,
 *   而三个芯片的时序/时钟要求不同(CC1101 ≤6.5MHz、Si4463 ~10MHz、nRF24 偏低),
 *   用软件 CS 才能做到"同一笔事务里把 CS 拉低→传输→拉高"这一确定序列,
 *   也方便将来在事务前后插射频门控检查。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define RF_SLOT_M1 1 /* Si4463 */
#define RF_SLOT_M2 2 /* CC1101 */
#define RF_SLOT_M3 3 /* nRF24L01P */

/* 共享 SPI2(docs/02-hardware.md §3.2;仅 SCK12 走 IO_MUX,13/14 走 GPIO 矩阵) */
#define PIN_SPI_SCK  12
#define PIN_SPI_MOSI 13
#define PIN_SPI_MISO 14

/* 各模块片选(低有效) */
#define PIN_M1_CS 15
#define PIN_M2_CS 16
#define PIN_M3_CS 17

/* 各模块供电轨使能(高有效;设计里是 LDO 的 EN,不是模块引脚) */
#define PIN_M1_EN 4
#define PIN_M2_EN 5
#define PIN_M3_EN 6

/* 初始化总线:CS 全部拉高(不选中)、EN 全部拉高(供电)。
 * ⚠️ 不碰 GPIO7(2.4G 总闸)与 GPIO8(RF_KILL):它们属于发射门控,
 *    初始化阶段必须保持 KILL(禁止发射)的默认态。 */
esp_err_t rf_spi_init(void);

/* 一笔事务:拉低 slot 的 CS → 全双工传输 → 拉高 CS。
 * rx 可为 NULL(只发不收);tx 可为 NULL(只收,发 0x00)。 */
esp_err_t rf_spi_xfer(int slot, const uint8_t *tx, uint8_t *rx, size_t len);

/* 同上,但可指定 CS 拉低到第一个 SCLK 之间的建立时间。
 * ⚠️ CC1101 要求 CSn 拉低后等 SO 变低(CHIP_RDYn)再给时钟;数据手册 Table 22
 *    在"不读 CHIP_RDYn"时给的最小 tSP,pd 是 **150µs**。普通事务 1µs 不够。 */
esp_err_t rf_spi_xfer_setup(int slot, const uint8_t *tx, uint8_t *rx, size_t len,
                            uint32_t cs_setup_us);

/* --- 接线诊断(在任何 SPI 初始化之前调用)--------------------------------
 * 三块模块同时读回 `0xFF` 时,无法区分三种原因:
 *   ① MISO 根本没接到模块   ② 模块没供电(高阻)   ③ 有东西把 MISO 硬拉高
 * 做法:把 MISO 配成普通输入,分别开内部下拉/上拉各读一次。
 *   pd=0, pu=1 → 该线**悬空**(没有任何驱动)= 多半没接上
 *   pd=1       → 有外部强上拉/被驱动为高(短路到 3.3V?5V 逻辑?)
 *   pu=0       → 被拉低(短路到 GND?)
 * ⚠️ 必须在 spi_bus_initialize 之前调用 —— 之后该脚被 SPI 外设接管。 */
typedef struct {
    int miso_pd; /* 开内部下拉时的电平 */
    int miso_pu; /* 开内部上拉时的电平 */
} rf_wire_t;

void rf_wire_check(rf_wire_t *out);

/* 线检原语(供 rf_probe 判断"模块到底有没有驱动 MISO")。
 * ⚠️ 只在诊断里用;正常事务一律走 rf_spi_xfer。 */
void rf_cs_set(int slot, int level);   /* 1 = 释放(高), 0 = 选中(低) */
void rf_miso_pull(int mode);           /* 0=浮空 1=上拉 2=下拉 */
int  rf_miso_level(void);

/* --- 收尾自检 ---
 * 释放 SPI 外设并把 MOSI/MISO 当普通 GPIO 用。
 * 现场用一根短接线把 GPIO13(MOSI)–GPIO14(MISO) 短接后,期望 hi=1、lo=0。
 * 不需要保留 SPI 总线:探测在启动时一次性完成。 */
void rf_spi_deinit(void);
void rf_loopback_test(int *hi, int *lo);
