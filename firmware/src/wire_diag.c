/*
 * wire_diag.c — 见 wire_diag.h。bring-up 诊断构建专用(默认不编译进去)。
 *
 * 电气判读为主;唯一会"写"的动作是按 Si446x 时序位操作发
 * POWER_UP + READ_CMD_BUFF + GET_PART_INFO(用于绕开 SPI 外设验证时序/接线)。
 * **全程不发射**:不发任何 TX 命令、不拉 CE、不碰 RF_KILL/2.4G 总闸。
 * ⚠️ 它会在 rf_spi_init() 之前给 M1 发 POWER_UP、把三路 EN 提前拉高,因此会
 *    改变上电时序,只用于现场排障,不能与正式探测混用(见 docs/03 §4/§6)。
 */
#include "wire_diag.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_rom_uart.h"

#include "rf_spi.h"

/* ---------------- 输出 ---------------- */

static void diag_out(const char *s)
{
    while (*s) {
        esp_rom_output_putc(*s++);
    }
}

static void diag_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        diag_out(buf);
    }
}

/* ---------------- GPIO 小工具 ---------------- */

enum { PULL_NONE = 0, PULL_UP = 1, PULL_DOWN = 2 };

static void pin_input(int pin, int pull)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pull == PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void pin_output(int pin, int level)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, level);
}

static int read_pull(int pin, int pull)
{
    pin_input(pin, pull);
    esp_rom_delay_us(800);
    return gpio_get_level(pin);
}

/* 返回: 0=外部拉低, 1=外部拉高, 2=悬空/高阻, 3=异常(跟随上下拉变化) */
static const char *classify(int d, int u)
{
    if (d == 1 && u == 1) return "tied-HIGH";
    if (d == 0 && u == 0) return "tied-LOW";
    if (d == 0 && u == 1) return "float";
    return "unstable";
}

/* ---------------- 位操作 SPI(Si446x 时序,约 60 kHz) ---------------- */

static void bb_setup(void)
{
    pin_output(PIN_SPI_SCK, 0);
    pin_output(PIN_SPI_MOSI, 0);
    pin_input(PIN_SPI_MISO, PULL_NONE);
    pin_output(PIN_M1_CS, 1);
    pin_output(PIN_M2_CS, 1);
    pin_output(PIN_M3_CS, 1);
    esp_rom_delay_us(10);
}

static void bb_cs(int slot, int level)
{
    int pin = (slot == 1) ? PIN_M1_CS : (slot == 2) ? PIN_M2_CS : PIN_M3_CS;
    gpio_set_level(pin, level ? 1 : 0);
}

/* M1 SDN 的 POR 脉冲(与 rf_spi_init 相同):GPIO9 必须**开漏** + 外部 100k 上拉。
 * 旧现场把 SDN 直接接 GND,推挽输出高会短路;开漏在旧接线下只是高阻(不损坏),
 * 新接线下得到 POR 高脉冲。位操作探测前必须真的释放过 SDN,否则芯片仍在 shutdown。 */
static void bb_sdn_por_pulse(void)
{
    gpio_config_t sdn = {
        .pin_bit_mask = (1ULL << PIN_M1_SDN),
        .mode         = GPIO_MODE_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&sdn);
    gpio_set_level(PIN_M1_SDN, 1); /* 释放,靠外部 100k 上拉 */
    esp_rom_delay_us(200);
    gpio_set_level(PIN_M1_SDN, 0); /* 主动拉低,触发 POR 启动 */
    esp_rom_delay_us(10000);
}

/* CPOL=0/CPHA=0:上升沿采样 MISO。每半周期 8µs(≈62 kHz),不依赖 SPI 外设。 */
static void bb_xfer(int slot, const uint8_t *tx, size_t n, uint8_t *rx)
{
    if (rx != NULL) {
        memset(rx, 0, n);
    }
    bb_cs(slot, 0);
    esp_rom_delay_us(20);
    for (size_t i = 0; i < n; i++) {
        uint8_t b = (tx != NULL) ? tx[i] : 0x00;
        for (int bit = 7; bit >= 0; bit--) {
            gpio_set_level(PIN_SPI_MOSI, (b >> bit) & 1);
            esp_rom_delay_us(8);
            gpio_set_level(PIN_SPI_SCK, 1);
            esp_rom_delay_us(8);
            if (rx != NULL) {
                rx[i] |= (uint8_t)(gpio_get_level(PIN_SPI_MISO) << bit);
            }
            gpio_set_level(PIN_SPI_SCK, 0);
            esp_rom_delay_us(8);
        }
    }
    esp_rom_delay_us(20);
    bb_cs(slot, 1);
    esp_rom_delay_us(20);
}

static void dump_hex(const char *tag, const uint8_t *b, size_t n)
{
    diag_printf("# dbg %s:", tag);
    for (size_t i = 0; i < n; i++) {
        diag_printf(" %02X", b[i]);
    }
    diag_out("\r\n");
}

/* 用位操作完整跑一遍 Si446x 的 POWER_UP + GET_PART_INFO。 */
static void bitbang_si446x(void)
{
    bb_setup();
    /* 0. SDN POR 脉冲:释放(上拉)≥200µs → 低 → ≥10ms,再 POWER_UP。 */
    bb_sdn_por_pulse();

    /* 1. POWER_UP(30 MHz 晶振) */
    const uint8_t power_up[7] = { 0x02, 0x01, 0x00, 0x01, 0xC9, 0xC3, 0x80 };
    bb_xfer(1, power_up, sizeof(power_up), NULL);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* 2. 轮询 READ_CMD_BUFF 的 CTS */
    uint8_t rx[10];
    int attempt = -1;
    for (int i = 0; i < 200; i++) {
        const uint8_t tx[2] = { 0x44, 0x00 };
        bb_xfer(1, tx, sizeof(tx), rx);
        if (rx[1] == 0xFF) {
            attempt = i;
            break;
        }
        esp_rom_delay_us(100);
    }
    diag_printf("# dbg bb M1 CTS: attempt=%d rx0=%02X rx1=%02X\r\n",
                attempt, rx[0], rx[1]);
    if (attempt < 0) {
        diag_out("# dbg bb M1: no CTS (chip never ready / not selected / no power)\r\n");
        return;
    }

    /* 3. GET_PART_INFO 命令(单独一笔) */
    const uint8_t cmd[1] = { 0x01 };
    bb_xfer(1, cmd, sizeof(cmd), NULL);

    /* 4. 轮询 READ_CMD_BUFF 并读 8 字节响应 */
    for (int i = 0; i < 200; i++) {
        uint8_t tx[10] = { 0x44 };
        uint8_t buf[10] = { 0 };
        bb_xfer(1, tx, sizeof(tx), buf);
        if (buf[1] == 0xFF) {
            dump_hex("bb M1 PART_INFO raw", buf, sizeof(buf));
            uint16_t part = (uint16_t)(((uint16_t)buf[3] << 8) | buf[4]);
            diag_printf("# dbg bb M1 part=0x%04X (期望 0x4463)\r\n", part);
            return;
        }
        esp_rom_delay_us(100);
    }
    diag_out("# dbg bb M1: PART_INFO response never ready\r\n");
}

/* ---------------- SPI 外设回环 ---------------- */

static void spi_peripheral_loopback(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        diag_printf("# dbg spiloop: bus init FAIL err=%d\r\n", (int)err);
        return;
    }
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    spi_device_handle_t h = NULL;
    err = spi_bus_add_device(SPI2_HOST, &dev, &h);
    if (err != ESP_OK || h == NULL) {
        diag_printf("# dbg spiloop: add dev FAIL err=%d\r\n", (int)err);
        (void)spi_bus_free(SPI2_HOST);
        return;
    }

    const uint8_t tx[4] = { 0xA5, 0x3C, 0x0F, 0xF0 };
    uint8_t rx[4] = { 0 };
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    err = spi_device_polling_transmit(h, &t);
    dump_hex("spiloop tx", tx, sizeof(tx));
    dump_hex("spiloop rx", rx, sizeof(rx));
    diag_printf("# dbg spiloop: tx=%d err=%d match=%d (1=13-14 已短接且 SPI 外设正常)\r\n",
                (int)(sizeof(tx)), (int)err, (memcmp(tx, rx, sizeof(tx)) == 0) ? 1 : 0);

    (void)spi_bus_remove_device(h);
    (void)spi_bus_free(SPI2_HOST);
    esp_rom_delay_us(200);
}

/* 恢复被诊断借用的引脚。必须在 rf_spi_init() 之前把电气状态交回一个确定值:
 *   - CS 全部输出高(不选中);
 *   - EN 全部输出高(**保持模块供电**:若在这里断电,rf_spi_init 会重新上电,
 *     等于把 POR 窗口再走一遍,反倒制造 M-10 描述的那种额外时序扰动);
 *   - SCK/MOSI 输出低、MISO 高阻输入(随后由 spi_bus_initialize 接管)。 */
static void diag_restore(void)
{
    pin_output(PIN_M1_CS, 1);
    pin_output(PIN_M2_CS, 1);
    pin_output(PIN_M3_CS, 1);
    pin_output(PIN_M1_EN, 1);
    pin_output(PIN_M2_EN, 1);
    pin_output(PIN_M3_EN, 1);
    pin_output(PIN_SPI_SCK, 0);
    pin_output(PIN_SPI_MOSI, 0);
    pin_input(PIN_SPI_MISO, PULL_NONE);
}

/* ---------------- 对外 ---------------- */

void wire_diag_run(void)
{
    diag_out("\r\n# dbg === wire diag start (bring-up build only) ===\r\n");

    /* 0. 先建立安全的空闲态:CS 高、EN 高(与 rf_spi_init 相同的前置)。 */
    pin_output(PIN_M1_CS, 1);
    pin_output(PIN_M2_CS, 1);
    pin_output(PIN_M3_CS, 1);
    pin_output(PIN_M1_EN, 1);
    pin_output(PIN_M2_EN, 1);
    pin_output(PIN_M3_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(3)); /* 等供电轨(若有 LDO)稳定 */

    diag_printf("# dbg CS idle=%d,%d,%d EN=%d,%d,%d\r\n",
                gpio_get_level(PIN_M1_CS), gpio_get_level(PIN_M2_CS),
                gpio_get_level(PIN_M3_CS), gpio_get_level(PIN_M1_EN),
                gpio_get_level(PIN_M2_EN), gpio_get_level(PIN_M3_EN));

    /* 1. 空闲线判读:13(MOSI) / 14(MISO) / 12(SCK)。CS 全高。 */
    int p12d = read_pull(PIN_SPI_SCK, PULL_DOWN);
    int p12u = read_pull(PIN_SPI_SCK, PULL_UP);
    int p13d = read_pull(PIN_SPI_MOSI, PULL_DOWN);
    int p13u = read_pull(PIN_SPI_MOSI, PULL_UP);
    int p14d = read_pull(PIN_SPI_MISO, PULL_DOWN);
    int p14u = read_pull(PIN_SPI_MISO, PULL_UP);
    diag_printf("# dbg idle g12 d=%d u=%d %s\r\n", p12d, p12u, classify(p12d, p12u));
    diag_printf("# dbg idle g13 d=%d u=%d %s\r\n", p13d, p13u, classify(p13d, p13u));
    diag_printf("# dbg idle g14 d=%d u=%d %s\r\n", p14d, p14u, classify(p14d, p14u));

    /* 2. 选中各槽位后,再看 13/14 是否被模块驱动(能发现 MOSI/MISO 反接/短路)。 */
    for (int slot = 1; slot <= 3; slot++) {
        bb_setup();
        bb_cs(slot, 0);
        esp_rom_delay_us(300);
        int d13 = read_pull(PIN_SPI_MOSI, PULL_DOWN);
        int u13 = read_pull(PIN_SPI_MOSI, PULL_UP);
        int d14 = read_pull(PIN_SPI_MISO, PULL_DOWN);
        int u14 = read_pull(PIN_SPI_MISO, PULL_UP);
        bb_setup();
        diag_printf("# dbg sel M%d: g13 d=%d u=%d %s | g14 d=%d u=%d %s\r\n",
                    slot, d13, u13, classify(d13, u13), d14, u14, classify(d14, u14));
    }
    bb_setup();
    bb_cs(1, 1);
    bb_cs(2, 1);
    bb_cs(3, 1);

    /* 3. 位操作直接跑 Si446x(等价于把 SPI 外设和 GPIO 矩阵完全绕开)。 */
    bitbang_si446x();

    /* 4. SPI2 外设回环:需现场用杜邦线短接 GPIO13–14,否则 match=0 正常。 */
    spi_peripheral_loopback();

    /* 5. 恢复被借用的引脚(CS 高、EN 高保持供电、SCK/MOSI 低、MISO 高阻),
     *    然后由调用方的 rf_spi_init() 重新接管。
     * ⚠️ 已删除旧的 "GPIO1/GPIO2 当模块 VCC/SDN 探点" 功能:M-09 ——
     *    GPIO1/GPIO2 是电池分压(ADC1_CH0)与 NTC(ADC1_CH1),借用会给出
     *    错误结论("VCC=3.3V")并诱导现场把 3.3V 短接到电池分压节点。 */
    diag_restore();

    diag_out("# dbg === wire diag end ===\r\n");
}
