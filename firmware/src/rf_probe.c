/*
 * rf_probe.c — 见 rf_probe.h。全部为只读事务(除 CC1101 的 SRES 复位与调谐)。
 */

#include "rf_probe.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_rom_sys.h"

#include "rf_spi.h"

/* ---------------- Si4463(M1) ---------------- */

#define SI446X_CMD_PART_INFO     0x01
#define SI446X_CMD_POWER_UP      0x02
#define SI446X_CMD_READ_CMD_BUFF 0x44

/* Si446x 的读命令是**两笔独立事务**(数据手册 Figure 4/5):
 *   ① CS 低 → 发命令字节 → CS 高;
 *   ② CS 低 → 发 READ_CMD_BUFF(0x44) → 读 CTS;CTS=0xFF 后继续读的才是响应数据 → CS 高。
 * ⚠️ 上一版把"命令 + 9 个 0x00"放在同一笔事务里读响应,不符合协议 —— 那 9 个字节
 *    不会被芯片当成 READ_CMD_BUFF,所以永远读到 0xFF。这是 2026-10-25 复测的关键更正。 */
static bool si446x_read_cmd_buff(uint8_t *out, size_t n)
{
    for (int attempt = 0; attempt < 200; attempt++) {
        uint8_t tx[10] = { SI446X_CMD_READ_CMD_BUFF };
        uint8_t rx[10] = {0};
        if (rf_spi_xfer(RF_SLOT_M1, tx, rx, 2 + n) == ESP_OK && rx[1] == 0xFF) {
            if (out != NULL && n > 0) {
                memcpy(out, &rx[2], n);
            }
            return true;
        }
        esp_rom_delay_us(50);
    }
    return false;
}

static void si4463_probe(rf_mod_t *m)
{
    /* 等命令缓冲就绪 → POWER_UP(AS10 模块用 30MHz 晶振,官方手册)→ 读 PART_INFO。
     * POR 之后需要 POWER_UP 才能完成初始化。 */
    (void)si446x_read_cmd_buff(NULL, 0);
    const uint8_t power_up[7] = { SI446X_CMD_POWER_UP, 0x01, 0x00,
                                  0x01, 0xC9, 0xC3, 0x80 };
    if (rf_spi_xfer(RF_SLOT_M1, power_up, NULL, sizeof(power_up)) != ESP_OK) {
        return;
    }
    if (!si446x_read_cmd_buff(NULL, 0)) {
        return;
    }

    const uint8_t cmd = SI446X_CMD_PART_INFO;
    if (rf_spi_xfer(RF_SLOT_M1, &cmd, NULL, 1) != ESP_OK) {
        return;
    }
    uint8_t resp[8] = {0};
    if (!si446x_read_cmd_buff(resp, sizeof(resp))) {
        return;
    }

    /* resp = CHIPREV, PART[15:8], PART[7:0], PBUILD, ID…(PART 是**大端**) */
    m->id0 = 0xFF; /* CTS 已确认 */
    uint16_t part = (uint16_t)(((uint16_t)resp[1] << 8) | resp[2]);
    m->id1 = (uint8_t)(part & 0xFF);
    m->id2 = (uint8_t)(part >> 8);
    m->present = (part == 0x4463);
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
    /* CC1101:CSn 拉低后要等 SO 变低(CHIP_RDYn)再给 SCLK;不读 CHIP_RDYn 时
     * 数据手册 Table 22 给的最小 tSP,pd = 150µs。1µs 不够,这也是复测更正点。 */
    (void)rf_spi_xfer_setup(RF_SLOT_M2, &t, NULL, 1, 200);
}

static uint8_t cc1101_read(uint8_t addr)
{
    /* 读事务:首字节是 header(同时移出状态字节),第二字节才是数据 */
    for (int attempt = 0; attempt < 10; attempt++) {
        const uint8_t tx[2] = { (uint8_t)(addr | 0x80), 0x00 };
        uint8_t rx[2] = {0, 0};
        if (rf_spi_xfer_setup(RF_SLOT_M2, tx, rx, 2, 200) != ESP_OK) {
            return 0xFF;
        }
        /* rx[0] = 状态字节;bit7 = CHIP_RDYn,0 = 晶振稳定、SPI 可访问 */
        if ((rx[0] & 0x80) == 0) {
            return rx[1];
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return 0xFF;
}

static void cc1101_write(uint8_t addr, uint8_t val)
{
    const uint8_t tx[2] = { (uint8_t)(addr & 0x3F), val };
    (void)rf_spi_xfer_setup(RF_SLOT_M2, tx, NULL, 2, 200);
}

static void cc1101_probe(rf_mod_t *m)
{
    cc1101_strobe(CC1101_SRES);      /* 软复位,回到已知状态 */
    vTaskDelay(pdMS_TO_TICKS(5));    /* SRES 后等晶振/状态机稳定 */

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


/* 线检:把该槽位 CS 拉低,再用"先上拉后下拉"两次读数判断 MISO 上到底有没有驱动源。
 * 为什么必须在 CS 低时做:芯片未被选中时 SDO/MISO 是**高阻**,悬空才是正常;
 * 只有选中后仍悬空,才说明 CS 或 MISO 没接、或模块没上电。 */
static void line_check(rf_mod_t *m, int slot)
{
    rf_cs_set(slot, 1);
    rf_miso_pull(0);
    esp_rom_delay_us(200);

    rf_cs_set(slot, 0);              /* 选中该模块 */
    esp_rom_delay_us(100);
    rf_miso_pull(1);
    esp_rom_delay_us(800);
    m->cs_pu = rf_miso_level();
    rf_miso_pull(2);
    esp_rom_delay_us(800);
    m->cs_pd = rf_miso_level();

    rf_cs_set(slot, 1);
    rf_miso_pull(0);
    esp_rom_delay_us(100);
}


/* SPI 自证:换一种上下拉再发一次事务,看读回值是否跟着走。
 * 用途是把"模块没响应"和"SPI 采样坏了"区分开 —— 两者的现象都是全 0xFF。 */
static uint8_t line_sample(int slot, int pull)
{
    rf_miso_pull(pull);
    esp_rom_delay_us(500);
    const uint8_t tx[2] = {0xB1, 0x00}; /* 随便一个读命令,只为产生 16 个时钟 */
    uint8_t rx[2] = {0, 0};
    (void)rf_spi_xfer(slot, tx, rx, 2);
    return rx[1];
}

void rf_probe_run(rf_probe_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->m[0].freq_hz[0] = 0; /* M1 不做 RSSI */
    line_check(&out->m[0], RF_SLOT_M1);
    si4463_probe(&out->m[0]);

    rf_mod_t *m2 = &out->m[1];
    out->m[0].spi_up   = line_sample(RF_SLOT_M1, 1);
    out->m[0].spi_down = line_sample(RF_SLOT_M1, 2);
    rf_miso_pull(0);

    line_check(m2, RF_SLOT_M2);
    cc1101_probe(m2);
    if (m2->present) {
        const uint32_t pts[RF_PROBE_POINTS] = { 387000000u, 433920000u, 464000000u };
        for (int i = 0; i < RF_PROBE_POINTS; i++) {
            m2->freq_hz[i] = pts[i];
            m2->rssi_ok[i] = cc1101_sample_rssi(pts[i], &m2->rssi_dbm[i],
                                                &m2->rssi_raw[i]);
        }
    }

    m2->spi_up   = line_sample(RF_SLOT_M2, 1);
    m2->spi_down = line_sample(RF_SLOT_M2, 2);
    rf_miso_pull(0);

    line_check(&out->m[2], RF_SLOT_M3);
    nrf24_probe(&out->m[2]);

    /* 收尾:释放 SPI 外设,做 GPIO13(MOSI)→GPIO14(MISO) 短接自检。 */
    rf_spi_deinit();
    rf_loopback_test(&out->loop_hi, &out->loop_lo);
}
