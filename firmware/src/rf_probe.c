/*
 * rf_probe.c — 见 rf_probe.h。
 *
 * 安全纪律(每一条都有审计依据):
 *   - **不发射**:M1 只发 POWER_UP/GET_PART_INFO/READ_CMD_BUFF;M2 只发
 *     SRES/SIDLE/SFRX/SRX 与配置寄存器写(调谐);M3 只发 R_REGISTER。
 *   - M1/M2 的探测结束都回到 IDLE;不发 0xB1(对 nRF24 = W_TX_PAYLOAD_NOACK)。
 *   - CC1101 状态寄存器必须用 `0xC0 | addr` 读(0x30–0x3D、burst=1 选状态寄存器)。
 *     用 0x80|addr 会被当成**命令选通**(0xB0=SRES、0xB1=SFSTXON、0xB4=SRX、
 *     0xB5=STX),既读不到数据,还可能触发发射选通。
 */

#include "rf_probe.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "rf_spi.h"

/* Si446x POWER_UP 的 CTS 轮询窗口。AN625 §3.1.1 明确"POWER_UP 可能比其他命令
 * 更久",Si446x 手册 Table 9 的 Shutdown→TX/RX 响应时间是 15ms,故给 100ms
 * 上限(原实现约 14ms,冷启动时不够,是 M-03/H-02 的一部分)。 */
#define SI446X_CTS_TIMEOUT_US 100000

/* ---------------- Si4463(M1) ---------------- */

#define SI446X_CMD_PART_INFO     0x01
#define SI446X_CMD_POWER_UP      0x02
#define SI446X_CMD_READ_CMD_BUFF 0x44

/* 读命令缓冲(数据手册 Figure 4/5):
 *   ① CS 低 → 发命令字节 → CS 高;
 *   ② CS 低 → 发 READ_CMD_BUFF(0x44) → 返回流第 2 字节是 CTS(0xFF=就绪),
 *      之后的字节才是响应 → CS 高。
 * `out_cts` 始终回填**实际读到的 CTS 字节**(不再用常量冒充,M-02)。
 * n>0 时:响应全为 0xFF 视为"没有响应"(悬空 MISO 与空闲高都是 0xFF)。 */
static bool si446x_read_cmd_buff(uint8_t *out, size_t n, uint8_t *out_cts)
{
    const int64_t deadline = esp_timer_get_time() + SI446X_CTS_TIMEOUT_US;

    while (esp_timer_get_time() < deadline) {
        uint8_t tx[10] = { SI446X_CMD_READ_CMD_BUFF };
        uint8_t rx[10] = {0};
        if (rf_spi_xfer(RF_SLOT_M1, tx, rx, 2 + n) == ESP_OK) {
            if (out_cts != NULL) {
                *out_cts = rx[1];
            }
            if (rx[1] == 0xFF) {
                bool all_ff = true;
                for (size_t i = 0; i < n; i++) {
                    if (rx[2 + i] != 0xFF) {
                        all_ff = false;
                        break;
                    }
                }
                if (n == 0 || !all_ff) {
                    if (out != NULL && n > 0) {
                        memcpy(out, &rx[2], n);
                    }
                    return true;
                }
            }
        }
        esp_rom_delay_us(50);
    }
    return false;
}

static void si4463_probe(rf_mod_t *m)
{
    /* POR 之后需要 POWER_UP 才能完成初始化。先等命令缓冲就绪(可选)。 */
    (void)si446x_read_cmd_buff(NULL, 0, &m->cts);

    const uint8_t power_up[7] = { SI446X_CMD_POWER_UP, 0x01, 0x00,
                                  0x01, 0xC9, 0xC3, 0x80 };
    if (rf_spi_xfer(RF_SLOT_M1, power_up, NULL, sizeof(power_up)) != ESP_OK) {
        return;
    }
    if (!si446x_read_cmd_buff(NULL, 0, &m->cts)) {
        return;
    }

    const uint8_t cmd = SI446X_CMD_PART_INFO;
    if (rf_spi_xfer(RF_SLOT_M1, &cmd, NULL, 1) != ESP_OK) {
        return;
    }
    uint8_t resp[8] = {0};
    if (!si446x_read_cmd_buff(resp, sizeof(resp), &m->cts)) {
        return;
    }

    /* resp = CHIPREV, PART[15:8], PART[7:0], PBUILD, ID…(PART 是**大端**) */
    m->id0 = resp[0]; /* CHIPREV:真实响应字节,不是伪造的 CTS 常量 */
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

/* 状态寄存器(0x30–0x3D,只读;读头 = 0xC0|addr)。 */
#define CC1101_REG_PARTNUM   0x30
#define CC1101_REG_VERSION   0x31
#define CC1101_REG_RSSI      0x34
#define CC1101_REG_MARCSTATE 0x35

/* 配置寄存器(0x00–0x2E,读头 = 0x80|addr,写头 = addr&0x3F)。 */
#define CC1101_REG_FREQ2     0x0D
#define CC1101_REG_FREQ1     0x0E
#define CC1101_REG_FREQ0     0x0F
#define CC1101_REG_MCSM1     0x17
#define CC1101_REG_MCSM0     0x18

static void cc1101_strobe(uint8_t cmd)
{
    /* 白名单:探测路径只允许复位/空闲/清 RX FIFO/进入 RX。
     * 绝不允许 STX(0x35) 等任何发射选通 —— 探测"不发射"的硬保证(H-01/M-01)。 */
    if (cmd != CC1101_SRES && cmd != CC1101_SIDLE &&
        cmd != CC1101_SFRX && cmd != CC1101_SRX) {
        return;
    }
    uint8_t t = cmd;
    /* CC1101:CSn 拉低后要等 SO 变低(CHIP_RDYn)再给 SCLK;不读 CHIP_RDYn 时
     * 数据手册 Table 22 给的最小 tSP,pd = 150µs。1µs 不够,这也是复测更正点。 */
    (void)rf_spi_xfer_setup(RF_SLOT_M2, &t, NULL, 1, 200);
}

/* 读**配置寄存器**(0x00–0x2E):读头 0x80|addr。 */
static uint8_t cc1101_read(uint8_t addr)
{
    if (addr > 0x2E) {
        return 0xFF; /* 状态寄存器不走这里(见 cc1101_read_status) */
    }
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

/* 读**状态寄存器**(0x30–0x3D):必须用 0xC0|addr。
 * 数据手册 §10.2 / Table 44/45:0x30–0x3D 区间 burst 位为 1 才选中状态寄存器;
 * 为 0 时同地址是命令选通(0xB0=SRES、0xB1=SFSTXON、0xB4=SRX、0xB5=STX)。
 * ⚠️ 这里**只做读**,不写任何东西。 */
static uint8_t cc1101_read_status(uint8_t addr)
{
    if (addr < 0x30 || addr > 0x3D) {
        return 0xFF;
    }
    for (int attempt = 0; attempt < 10; attempt++) {
        const uint8_t tx[2] = { (uint8_t)(addr | 0xC0), 0x00 };
        uint8_t rx[2] = {0, 0};
        if (rf_spi_xfer_setup(RF_SLOT_M2, tx, rx, 2, 200) != ESP_OK) {
            return 0xFF;
        }
        if ((rx[0] & 0x80) == 0) {
            return rx[1];
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return 0xFF;
}

static void cc1101_write(uint8_t addr, uint8_t val)
{
    if (addr > 0x2E) {
        return; /* 只写配置寄存器;状态寄存器只读 */
    }
    const uint8_t tx[2] = { (uint8_t)(addr & 0x3F), val };
    (void)rf_spi_xfer_setup(RF_SLOT_M2, tx, NULL, 2, 200);
}

static void cc1101_probe(rf_mod_t *m)
{
    cc1101_strobe(CC1101_SRES);      /* 软复位,回到已知状态 */
    vTaskDelay(pdMS_TO_TICKS(5));    /* SRES 后等晶振/状态机稳定 */

    m->id0 = cc1101_read_status(CC1101_REG_PARTNUM);
    m->id1 = cc1101_read_status(CC1101_REG_VERSION);
    m->present = (m->id0 == 0x00) && (m->id1 == 0x14 || m->id1 == 0x04);

    /* 探测结束必须是 IDLE:不留在 RX/TX,也不留任何发射可能。 */
    cc1101_strobe(CC1101_SIDLE);
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
    /* MCSM1(0x17):bits5:4 CCA_MODE、bits3:2 RXOFF_MODE、bits1:0 TXOFF_MODE。
     * 0x0C = CCA_MODE=00、RXOFF_MODE=11(收完停在 RX)、TXOFF_MODE=00。
     * ⚠️ 旧值 0x30 是 RXOFF_MODE=00(IDLE),与"收完停在 RX"的注释相反(L-09)。 */
    cc1101_write(CC1101_REG_MCSM1, 0x0C);

    cc1101_strobe(CC1101_SFRX);
    cc1101_strobe(CC1101_SRX);

    /* 等状态机进 RX(0x0D);校准通常 <1ms,这里给 50ms 上限 */
    bool in_rx = false;
    for (int i = 0; i < 50; i++) {
        if (cc1101_read_status(CC1101_REG_MARCSTATE) == 0x0D) {
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

    uint8_t r = cc1101_read_status(CC1101_REG_RSSI);
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

/* ---------------- 诊断辅助(事实记录,不做因果推断) ---------------- */

/* 线检:把该槽位 CS 拉低,再用"先上拉后下拉"两次读数看 MISO 上有没有驱动源。
 * 为什么必须在 CS 低时做:芯片未被选中时 SDO/MISO 是**高阻**,悬空才是正常。
 * ⚠️ 读数是事实:相等 = 被外部驱动;不等 = 读数跟随内部上下拉(无外部驱动)。
 *    不能仅凭此断言"CS/MISO 没接或模块没上电" —— POR 未完成/未 POWER_UP 时
 *    SDO 同样不被驱动,必须结合供电、CTS/ID 读数一起判读。 */
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

/* 各槽位的**只读**自证命令(产生 16 个时钟,用来验证读回是否随 MISO 上下拉变化)。
 *   M1 Si446x : 0x44 READ_CMD_BUFF —— 只读命令缓冲
 *   M2 CC1101 : 0xC0|0x30 状态寄存器 PARTNUM —— 只读
 *   M3 nRF24  : 0xFF NOP —— 只读 STATUS
 * ⚠️ 禁止再用 0xB1:对 nRF24 它是 W_TX_PAYLOAD_NOACK(写 TX FIFO!),对 CC1101
 *    是未定义地址,M1 也不认。这是 M-01 的核心修复。 */
static uint8_t line_sample(int slot, int pull)
{
    static const uint8_t safe_cmd[3] = {
        0x44,                    /* M1: READ_CMD_BUFF */
        (uint8_t)(0xC0 | 0x30),  /* M2: 状态寄存器 PARTNUM */
        0xFF,                    /* M3: NOP */
    };
    if (slot < RF_SLOT_M1 || slot > RF_SLOT_M3) {
        return 0xFF;
    }
    rf_miso_pull(pull);
    esp_rom_delay_us(500);
    const uint8_t tx[2] = { safe_cmd[slot - 1], 0x00 };
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

    /* M1:先探测,再做线检(M-02:line_check 必须在 POWER_UP/ID 读取**之后**,
     * 否则芯片还没初始化,结论没有意义)。 */
    si4463_probe(&out->m[0]);
    line_check(&out->m[0], RF_SLOT_M1);
    out->m[0].spi_up   = line_sample(RF_SLOT_M1, 1);
    out->m[0].spi_down = line_sample(RF_SLOT_M1, 2);
    rf_miso_pull(0);

    /* M2 */
    rf_mod_t *m2 = &out->m[1];
    cc1101_probe(m2);
    line_check(m2, RF_SLOT_M2);
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

    /* M3:GPIO7=0 且 RF_KILL=0 ⇒ CE=AND(7,8)=0 ⇒ 只读探测安全。 */
    nrf24_probe(&out->m[2]);
    line_check(&out->m[2], RF_SLOT_M3);
    out->m[2].spi_up   = line_sample(RF_SLOT_M3, 1);
    out->m[2].spi_down = line_sample(RF_SLOT_M3, 2);
    rf_miso_pull(0);

    /* 收尾:释放 SPI 外设,做 GPIO13(MOSI)→GPIO14(MISO) 短接自检。 */
    rf_spi_deinit();
    rf_loopback_test(&out->loop_hi, &out->loop_lo);
}
