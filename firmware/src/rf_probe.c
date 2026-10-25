/*
 * rf_probe.c — 见 rf_probe.h。全部为只读事务(除 CC1101 的 SRES 复位与调谐)。
 */

#include "rf_probe.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rf_spi.h"

/* ---------------- Si4463(M1) ---------------- */

static void si4463_probe(rf_mod_t *m)
{
    /* GET_PART_INFO(0x01),无参数。
     * Si446x 的 SPI 约定:命令字节之后先回一个 CTS(0xFF = 芯片就绪),
     * 随后才是 8 字节响应:CHIPREV, PART[7:0], PART[15:8], PBUILD, ID, CUST, ROMID… */
    const uint8_t tx[10] = {0x01, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t rx[10] = {0};
    if (rf_spi_xfer(RF_SLOT_M1, tx, rx, sizeof(tx)) != ESP_OK) {
        return;
    }
    m->id0 = rx[1];                              /* CTS */
    m->id1 = rx[3];                              /* PART 低字节 */
    m->id2 = rx[4];                              /* PART 高字节 */
    uint16_t part = (uint16_t)(m->id1 | (m->id2 << 8));
    m->present = (m->id0 == 0xFF) && (part == 0x4463);
}

/* ---------------- CC1101(M2) ---------------- */

#define CC1101_SRES  0x30
#define CC1101_SIDLE 0x36
#define CC1101_SFRX  0x3A
#define CC1101_SRX   0x34
#define CC1101_REG_MARCSTATE 0x35
#define CC1101_REG_RSSI      0x34
#define CC1101_REG_PARTNUM   0x30
#define CC1101_REG_VERSION   0x31
#define CC1101_REG_FREQ2     0x0D
#define CC1101_REG_FREQ1     0x0E
#define CC1101_REG_FREQ0     0x0F
#define CC1101_REG_MCSM1     0x17
#define CC1101_REG_MCSM0     0x18

static void cc1101_strobe(uint8_t cmd)
{
    uint8_t t = cmd;
    (void)rf_spi_xfer(RF_SLOT_M2, &t, NULL, 1);
}

static uint8_t cc1101_read(uint8_t addr)
{
    /* 读事务:首字节是 header(同时移出状态字节),第二字节才是数据 */
    const uint8_t tx[2] = { (uint8_t)(addr | 0x80), 0x00 };
    uint8_t rx[2] = {0, 0};
    if (rf_spi_xfer(RF_SLOT_M2, tx, rx, 2) != ESP_OK) {
        return 0xFF;
    }
    return rx[1];
}

static void cc1101_write(uint8_t addr, uint8_t val)
{
    const uint8_t tx[2] = { (uint8_t)(addr & 0x3F), val };
    (void)rf_spi_xfer(RF_SLOT_M2, tx, NULL, 2);
}

static void cc1101_probe(rf_mod_t *m)
{
    cc1101_strobe(CC1101_SRES);      /* 软复位,回到已知状态 */
    vTaskDelay(pdMS_TO_TICKS(2));    /* SRES 需要一点时间,别紧跟着发命令 */

    m->id0 = cc1101_read(CC1101_REG_PARTNUM);
    m->id1 = cc1101_read(CC1101_REG_VERSION);
    m->present = (m->id0 == 0x00) && (m->id1 == 0x14 || m->id1 == 0x04);
}

/* 在给定频率上做一次 RSSI 采样。返回 false = 没等进 RX 态。 */
static bool cc1101_sample_rssi(uint32_t freq_hz, int *out_dbm, uint8_t *out_raw)
{
    /* FREQ = f_carrier / 26MHz * 2^16 */
    uint32_t f = (uint32_t)(((uint64_t)freq_hz << 16) / 26000000ULL);

    cc1101_strobe(CC1101_SIDLE);
    cc1101_write(CC1101_REG_FREQ2, (uint8_t)(f >> 16));
    cc1101_write(CC1101_REG_FREQ1, (uint8_t)(f >> 8));
    cc1101_write(CC1101_REG_FREQ0, (uint8_t)(f));
    cc1101_write(CC1101_REG_MCSM0, 0x18); /* IDLE→RX 时自动校准 */
    cc1101_write(CC1101_REG_MCSM1, 0x30); /* 收完停在 RX */

    cc1101_strobe(CC1101_SFRX);
    cc1101_strobe(CC1101_SRX);

    /* 等状态机进 RX(0x0D);校准通常 <1ms,这里给 50ms 上限 */
    bool in_rx = false;
    for (int i = 0; i < 50; i++) {
        if (cc1101_read(CC1101_REG_MARCSTATE) == 0x0D) {
            in_rx = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!in_rx) {
        cc1101_strobe(CC1101_SIDLE);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(2)); /* 让 AGC 稳定,RSSI 才有意义 */

    uint8_t r = cc1101_read(CC1101_REG_RSSI);
    cc1101_strobe(CC1101_SIDLE);

    int dbm = (r >= 128) ? (((int)r - 256) / 2 - 74) : ((int)r / 2 - 74);
    *out_dbm = dbm;
    *out_raw = r;
    return true;
}

/* ---------------- nRF24L01(M3) ---------------- */

static uint8_t nrf24_read(uint8_t reg)
{
    const uint8_t tx[2] = { (uint8_t)(reg & 0x1F), 0x00 }; /* R_REGISTER */
    uint8_t rx[2] = {0, 0};
    if (rf_spi_xfer(RF_SLOT_M3, tx, rx, 2) != ESP_OK) {
        return 0xFF;
    }
    return rx[1];
}

static void nrf24_probe(rf_mod_t *m)
{
    m->id0 = nrf24_read(0x00); /* CONFIG,复位值 0x08 */
    m->id1 = nrf24_read(0x07); /* STATUS,常见 0x0E */
    /* 0xFF/0x00 是"没芯片"的典型读数(MISO 被拉高/拉低) */
    m->present = (m->id0 == 0x08);
}

/* ---------------- 对外 ---------------- */

void rf_probe_run(rf_probe_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->m[0].freq_hz[0] = 0; /* M1 不做 RSSI */
    si4463_probe(&out->m[0]);

    rf_mod_t *m2 = &out->m[1];
    cc1101_probe(m2);
    if (m2->present) {
        const uint32_t pts[RF_PROBE_POINTS] = { 387000000u, 433920000u, 464000000u };
        for (int i = 0; i < RF_PROBE_POINTS; i++) {
            m2->freq_hz[i] = pts[i];
            m2->rssi_ok[i] = cc1101_sample_rssi(pts[i], &m2->rssi_dbm[i],
                                                &m2->rssi_raw[i]);
        }
    }

    nrf24_probe(&out->m[2]);
}
