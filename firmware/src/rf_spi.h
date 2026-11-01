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

/* 2.4G 总闸(GPIO7):高 = 软件允许 2.4G 模块工作;上电默认必须为 0。
 * 门控链:**M3 CE = AND(GPIO7, GPIO8/RF_KILL)**,AND 输出直接接 AS01 pin3 CE。
 * 因此 GPIO7=0 或 RF_KILL=0 ⇒ CE=0 ⇒ nRF24 处于 power-down、PA 不工作 ⇒
 * M3 可以在 KILL 保持低的安全状态下由 GPIO6 上电做 SPI 只读探测。
 * ⚠️ 固件**没有** CE GPIO —— CE 由 AND 门驱动,不得新增。 */
#define PIN_RF_GATE 7

/* M1(Si446x)SDN(GPIO9):**开漏输出** + 外部 100k 上拉到 3.3V
 * ⇒ 上电默认 shutdown(安全)。固件上电时序:EN 拉高并等 ≥20ms 后,
 * GPIO9 释放(置 1,靠外部上拉为高)≥100µs → 主动拉低(置 0)→ 等 ≥10ms,
 * 才发 POWER_UP / 做 SPI 探测(Si446x 数据手册 POR 要求;AS10 模块手册图 5-1
 * 也把 SDN 接到 MCU I/O)。
 * 🔴 必须是开漏,**不得改成推挽**:现场既有接线把 SDN 直接接 GND,推挽输出高会
 *    与 GND 短路(可能损坏引脚)。开漏在旧接线(SDN→GND)下退化为"无高脉冲、
 *    保持低电平",在新接线(SDN→GPIO9+100k)下给出正确 POR 脉冲。 */
#define PIN_M1_SDN 9

/* 初始化总线。顺序是硬性时序要求(docs/03-firmware.md §4):
 *   1. CS 三个输出**预置高**后再切输出方向(避免复位后短暂误选中);
 *   2. 三路 EN 预置高再切输出,等 **≥20ms** 让 LDO/模块 VDD 与 POR 完成
 *      (Si446x tPORH ≥10ms、nRF24 T_POR max 10.3ms、CC1101 ramp ≤5ms);
 *   3. M1 SDN 高 ≥100µs → 低 → 等 **≥10ms**;
 *   4. 再初始化 SPI2(1MHz、mode0、软件 CS)。
 * ⚠️ GPIO7(2.4G 总闸)/GPIO8(RF_KILL)由 main.c 在上电最早期显式驱动为 0,
 *    本函数**不得**把它们拉高。M3 能在 CE=0 的安全态下上电探测正依赖这一点。 */
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

/* 线检原语(供 rf_probe 判断"选中后 MISO 上到底有没有驱动源")。
 * ⚠️ 只在诊断里用;正常事务一律走 rf_spi_xfer。 */
void rf_cs_set(int slot, int level);   /* 1 = 释放(高), 0 = 选中(低) */
void rf_miso_pull(int mode);           /* 0=浮空 1=上拉 2=下拉 */
int  rf_miso_level(void);

/* --- 收尾自检 ---
 * 先 remove_device 再 free 总线,并把句柄置 NULL(之后不得再用)。
 * 现场用一根短接线把 GPIO13(MOSI)–GPIO14(MISO) 短接后,期望 hi=1、lo=0。
 * 不需要保留 SPI 总线:探测在启动时一次性完成。 */
void rf_spi_deinit(void);
void rf_loopback_test(int *hi, int *lo);
