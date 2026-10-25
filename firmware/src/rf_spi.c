/*
 * rf_spi.c — SPI2 总线与片选(见 rf_spi.h 的说明)
 */

#include "rf_spi.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_rom_uart.h"

static spi_device_handle_t s_spi;
static const int s_cs_pin[3] = { PIN_M1_CS, PIN_M2_CS, PIN_M3_CS };

esp_err_t rf_spi_init(void)
{
    /* 1. 先把三个 CS 配成输出并**立即拉高**:复位后 GPIO 是高阻,
     *    悬空的 CS 可能让模块把总线噪声当命令。 */
    const uint64_t cs_mask = (1ULL << PIN_M1_CS) | (1ULL << PIN_M2_CS) |
                             (1ULL << PIN_M3_CS);
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

    /* 2. 三条供电轨使能拉高(设计的 LDO-A/B/C EN,高有效)。
     *    ⚠️ 面包板直连时这三根可能没接 —— 空着无害;接了就负责给模块上电。 */
    const uint64_t en_mask = (1ULL << PIN_M1_EN) | (1ULL << PIN_M2_EN) |
                             (1ULL << PIN_M3_EN);
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
    esp_rom_delay_us(2000); /* 等 LDO 输出稳定(若有 LDO 的话) */

    /* 3. 总线本体。1MHz 是三者共同的安全起点:
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
