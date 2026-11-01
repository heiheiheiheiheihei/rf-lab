/*
 * rf_spi.c — SPI2 总线与片选(见 rf_spi.h 的说明)
 *
 * 初始化时序是本板的 POR 生命线(docs/03-firmware.md §4),不要为了"启动快"缩小时延:
 *   EN 拉高 → ≥20ms → M1 SDN(开漏)释放 ≥100µs → 拉低 → ≥10ms → SPI 总线 → 探测。
 */

#include "rf_spi.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_rom_uart.h"

/* POR/上电稳定等待(见 rf_spi.h)。不要改小。 */
#define RFLAB_EN_SETTLE_US   20000 /* ≥20ms:覆盖 Si446x 10ms + nRF24 10.3ms + CC1101 5ms */
#define RFLAB_SDN_HIGH_US      100 /* ≥100µs:SDN 高电平宽度 */
#define RFLAB_SDN_SETTLE_US  10000 /* ≥10ms:SDN 释放到 POWER_UP 之间 */

static spi_device_handle_t s_spi;
static const int s_cs_pin[3] = { PIN_M1_CS, PIN_M2_CS, PIN_M3_CS };

/* M1 Si446x 的 SDN 上电脉冲(安全关键):
 *   1. GPIO9 配成**开漏输出**(GPIO_MODE_OUTPUT_OD),外部 100k 上拉到 3.3V;
 *   2. 置 1 = 释放总线(靠外部上拉拉高)→ 保持 ≥100µs;
 *   3. 置 0 = 主动拉低 → 等 ≥10ms。
 *
 * 🔴 为什么必须是开漏而不是推挽:现场既有硬件把 SDN 直接接到 GND。若 GPIO9 用
 *    推挽输出高电平,就会与 GND 形成直流通路(短路到地),既得不到高电平,也可能
 *    损坏引脚。开漏在两种接线下来都安全:
 *      - 旧接线(SDN→GND):置 1 只是高阻,引脚仍被 GND 拉低,无高电平、无短路;
 *      - 新接线(SDN→GPIO9 + 100k 上拉):置 1 = 释放 → 上拉得到高电平,POR 脉冲成立。
 *    即"旧板退化为没有脉冲、但不会损坏",新板得到正确 POR。 */
static void m1_sdn_pulse(void)
{
    gpio_config_t sdn = {
        .pin_bit_mask = (1ULL << PIN_M1_SDN),
        .mode         = GPIO_MODE_OUTPUT_OD, /* ⚠️ 开漏,禁止改成 GPIO_MODE_OUTPUT */
        .pull_up_en   = GPIO_PULLUP_DISABLE, /* 高电平由外部 100k 提供 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&sdn) != ESP_OK) {
        return;
    }
    gpio_set_level(PIN_M1_SDN, 1); /* 释放 → 外部上拉拉高 */
    esp_rom_delay_us(RFLAB_SDN_HIGH_US);
    gpio_set_level(PIN_M1_SDN, 0); /* 主动拉低 */
    esp_rom_delay_us(RFLAB_SDN_SETTLE_US);
}

esp_err_t rf_spi_init(void)
{
    /* 1. CS 三根低有效。**先写输出锁存再切输出方向**(L-03):
     *    复位后 GPIO 输出锁存默认 0,若先 gpio_config 再 set_level,中间会有一个
     *    极短的低电平窗口 —— 对低有效 CS 就是一次伪片选。 */
    const uint64_t cs_mask = (1ULL << PIN_M1_CS) | (1ULL << PIN_M2_CS) |
                             (1ULL << PIN_M3_CS);
    for (int i = 0; i < 3; i++) {
        gpio_set_level(s_cs_pin[i], 1); /* 预置锁存(此时脚还是输入,无输出) */
    }
    gpio_config_t cs_cfg = {
        .pin_bit_mask = cs_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    #ifdef RFLAB_MARKERS
    { const char *m = "[S]1 cs cfg\r\n"; while (*m) esp_rom_output_putc(*m++); }
    #endif
    esp_err_t err = gpio_config(&cs_cfg);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < 3; i++) {
        gpio_set_level(s_cs_pin[i], 1);
    }

    /* 2. 三条供电轨使能(设计的 LDO-A/B/C EN,高有效)。同样先预置高再切输出。
     *    ⚠️ M3 供电与 CE 无关:M3 CE = AND(GPIO7, GPIO8)。GPIO7 与 RF_KILL 在
     *    main.c 已显式驱动为 0 ⇒ CE=0 ⇒ nRF24 power-down,此时给 M3 上电只做
     *    SPI 只读探测是安全的(docs/02-hardware.md §3.3)。
     *    ⚠️ 面包板直连时这三根可能没接 —— 空着无害;接了就负责给模块上电。 */
    const uint64_t en_mask = (1ULL << PIN_M1_EN) | (1ULL << PIN_M2_EN) |
                             (1ULL << PIN_M3_EN);
    gpio_set_level(PIN_M1_EN, 1);
    gpio_set_level(PIN_M2_EN, 1);
    gpio_set_level(PIN_M3_EN, 1);
    gpio_config_t en_cfg = {
        .pin_bit_mask = en_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&en_cfg);
    if (err != ESP_OK) {
        return err;
    }
    for (int pin = PIN_M1_EN; pin <= PIN_M3_EN; pin++) {
        gpio_set_level(pin, 1);
    }
    /* H-02:EN 拉高后必须等 POR 完成再碰 SPI,否则 M1/M3 第一笔事务落在
     * 复位窗口里,读数全是 0xFF、探测必然失败。 */
    esp_rom_delay_us(RFLAB_EN_SETTLE_US);

    /* 3. M1 SDN(开漏):释放 ≥100µs → 拉低 → 等 ≥10ms(H-02)。 */
    m1_sdn_pulse();

    /* 4. 总线本体。1MHz 是三者共同的安全起点:
     *    CC1101 手册建议 ~1M(芯片上限 6.5MHz),nRF24 的 SPI 上限更低。 */
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_SPI_MOSI,
        .miso_io_num     = PIN_SPI_MISO,
        .sclk_io_num     = PIN_SPI_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000 * 1000,
        .mode           = 0, /* CPOL=0, CPHA=0:三个芯片都是模式 0 */
        .spics_io_num   = -1, /* CS 由软件控制 */
        .queue_size     = 1,
    };
    #ifdef RFLAB_MARKERS
    { const char *m = "[S]4 add dev\r\n"; while (*m) esp_rom_output_putc(*m++); }
    #endif
    return spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
}

void rf_wire_check(rf_wire_t *out)
{
    if (out == NULL) {
        return;
    }
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_SPI_MISO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in);
    esp_rom_delay_us(2000);
    out->miso_pd = gpio_get_level(PIN_SPI_MISO);

    in.pull_up_en   = GPIO_PULLUP_ENABLE;
    in.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&in);
    esp_rom_delay_us(2000);
    out->miso_pu = gpio_get_level(PIN_SPI_MISO);
    /* 该脚随后由 spi_bus_initialize 接管 */
}

esp_err_t rf_spi_xfer_setup(int slot, const uint8_t *tx, uint8_t *rx, size_t len,
                            uint32_t cs_setup_us)
{
    if (slot < RF_SLOT_M1 || slot > RF_SLOT_M3 || len == 0 || len > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    const int cs = s_cs_pin[slot - 1];
    gpio_set_level(cs, 0);
    esp_rom_delay_us(cs_setup_us); /* tCS 建立时间 */

    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);

    esp_rom_delay_us(1); /* tCS 保持时间 */
    gpio_set_level(cs, 1);
    return err;
}

esp_err_t rf_spi_xfer(int slot, const uint8_t *tx, uint8_t *rx, size_t len)
{
    return rf_spi_xfer_setup(slot, tx, rx, len, 1);
}

void rf_cs_set(int slot, int level)
{
    if (slot >= RF_SLOT_M1 && slot <= RF_SLOT_M3) {
        gpio_set_level(s_cs_pin[slot - 1], level ? 1 : 0);
    }
}

void rf_miso_pull(int mode)
{
    switch (mode) {
    case 1:  gpio_set_pull_mode(PIN_SPI_MISO, GPIO_PULLUP_ONLY);   break;
    case 2:  gpio_set_pull_mode(PIN_SPI_MISO, GPIO_PULLDOWN_ONLY); break;
    default: gpio_set_pull_mode(PIN_SPI_MISO, GPIO_FLOATING);      break;
    }
}

int rf_miso_level(void)
{
    return gpio_get_level(PIN_SPI_MISO);
}

void rf_spi_deinit(void)
{
    /* L-02:必须先摘掉 device 再释放总线,否则 s_spi 成为悬空句柄;
     * 之后若有人复用总线(重新 init / 调 rf_spi_xfer)会踩内存。 */
    if (s_spi != NULL) {
        (void)spi_bus_remove_device(s_spi);
        s_spi = NULL;
    }
    (void)spi_bus_free(SPI2_HOST);
}

void rf_loopback_test(int *hi, int *lo)
{
    /* MOSI 推挽输出、MISO 输入 + 内部下拉:没有短接时 hi 读到 0(区分于悬空)。 */
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_SPI_MOSI),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_SPI_MISO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in);
    esp_rom_delay_us(200);

    gpio_set_level(PIN_SPI_MOSI, 1);
    esp_rom_delay_us(200);
    *hi = gpio_get_level(PIN_SPI_MISO);

    gpio_set_level(PIN_SPI_MOSI, 0);
    esp_rom_delay_us(200);
    *lo = gpio_get_level(PIN_SPI_MISO);
}
