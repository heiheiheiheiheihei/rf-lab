/*
 * rf_probe.h — 三模块在线探测(阶段 2 第一步)
 *
 * 目的只有一个:**在没有驱动之前,先证明"接线 + 供电 + SPI 通路"是通的**。
 * 做法是读每个芯片"出厂就有、不需要配置"的标识寄存器:
 *   M1 Si4463   : GET_PART_INFO 命令(0x01)→ CTS 应 0xFF,PART 应 0x4463
 *   M2 CC1101   : PARTNUM(0x30)=0x00,VERSION(0x31)=0x14(CC1100 为 0x04)
 *   M3 nRF24L01 : CONFIG(0x00) 复位值 0x08,STATUS(0x07) 常见 0x0E
 *
 * M2 额外做一次**单点 RSSI 测量**(387 / 433.92 / 464 MHz):
 * 它证明的不只是 SPI,而是"调谐 + 接收链路"真的在工作。读数为噪声底也正常,
 * 因为测量时并没有要求空中有信号。
 *
 * ⚠️ 本文件**不做任何发射**:不写 TX FIFO、不拉 CE、不碰 RF_KILL/2.4G 总闸。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RF_PROBE_POINTS 3

typedef struct {
    bool     present;
    /* 芯片原始标识(按模块含义不同,probe 后打印出来便于人工判读) */
    uint8_t  id0;
    uint8_t  id1;
    uint8_t  id2;
    /* 仅 M2:三个频点的 RSSI */
    uint32_t freq_hz[RF_PROBE_POINTS];
    int      rssi_dbm[RF_PROBE_POINTS]; /* 仅在 rssi_ok 为真时有效 */
    uint8_t  rssi_raw[RF_PROBE_POINTS];
    bool     rssi_ok[RF_PROBE_POINTS];
    /* 线检:CS 拉低时,MISO 用上拉/下拉各读一次。
     * cs_pu == cs_pd ⇒ 有驱动源(模块在驱动这条线);
     * 不等 ⇒ 即使选中也悬空(CS/MISO 没接,或模块没上电)。 */
    int      cs_pu;
    int      cs_pd;
} rf_mod_t;

typedef struct {
    rf_mod_t m[3]; /* 0=M1(Si4463) 1=M2(CC1101) 2=M3(nRF24) */
} rf_probe_t;

/* 跑一遍探测。总耗时约 10ms(含一次 CC1101 校准)。可重复调用。 */
void rf_probe_run(rf_probe_t *out);
