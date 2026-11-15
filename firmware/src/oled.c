/*
 * oled — SSD1306 72×40 I2C 传输层实现(ESP-IDF 5.5 新 i2c_master API)。
 */
#include "oled.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oled_panel.h"

#define OLED_I2C_TIMEOUT_MS 50
#define OLED_INIT_DELAY_MS 100

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_ready;
static int s_hz;
static oled_stats_t s_stats;

static esp_err_t write_cmds(const uint8_t *cmds, size_t n)
{
    uint8_t buf[64];

    if (!s_dev || n + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_STATE;
    }
    buf[0] = 0x00; /* Co=0, D/C#=0:后续字节全是命令 */
    memcpy(&buf[1], cmds, n);
    return i2c_master_transmit(s_dev, buf, n + 1, OLED_I2C_TIMEOUT_MS);
}

static void cleanup(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    s_ready = false;
    s_hz = 0;
}

static esp_err_t try_init(int sda_gpio, int scl_gpio, int scl_hz)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_PANEL_ADDR,
        .scl_speed_hz = (uint32_t)scl_hz,
    };

    esp_err_t e = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (e != ESP_OK) {
        s_bus = NULL;
        return e;
    }

    e = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (e != ESP_OK) {
        s_dev = NULL;
        cleanup();
        return e;
    }

    /* 先只做地址探测:屏不在时不要把失败混进"初始化序列写错"里。 */
    e = i2c_master_probe(s_bus, OLED_PANEL_ADDR, OLED_I2C_TIMEOUT_MS);
    if (e != ESP_OK) {
        cleanup();
        return e;
    }

    e = write_cmds(k_oled_panel_init, sizeof(k_oled_panel_init));
    if (e != ESP_OK) {
        cleanup();
        return e;
    }

    /* 72×40 模块上电后需要较长建立时间(U8g2 也给 100ms);这里只付一次。 */
    vTaskDelay(pdMS_TO_TICKS(OLED_INIT_DELAY_MS));

    const uint8_t on = 0xAF;
    e = write_cmds(&on, 1);
    if (e != ESP_OK) {
        cleanup();
        return e;
    }

    s_hz = scl_hz;
    memset(&s_stats, 0, sizeof(s_stats));
    s_ready = true;
    return ESP_OK;
}

esp_err_t oled_init(int sda_gpio, int scl_gpio, int scl_hz)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t e = try_init(sda_gpio, scl_gpio, scl_hz);

    /*
     * 400 kHz 是 SSD1306 的常见上限,但 72×40 小板的走线/上拉不一定配合。
     * 失败就退回 100 kHz(Zephyr 01Space 板级定义用的标准速率)再试一次;
     * 只在这一步做一次降速,运行期不动态切换。
     */
    if (e != ESP_OK && scl_hz > 100000) {
        e = try_init(sda_gpio, scl_gpio, 100000);
    }
    return e;
}

bool oled_is_ready(void)
{
    return s_ready;
}

int oled_bus_hz(void)
{
    return s_hz;
}

esp_err_t oled_flush(const gfx_t *g)
{
    /* 命令窗口:列 28..99(0x1C..0x63),页 0..4。 */
    const uint8_t win[] = {
        0x00, /* command stream */
        0x21, OLED_PANEL_COL_OFFSET, OLED_PANEL_COL_END,
        0x22, 0x00, OLED_PANEL_PAGES - 1,
    };
    uint8_t buf[1 + GFX_FB_BYTES];

    if (!s_ready || !g) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t e = i2c_master_transmit(s_dev, win, sizeof(win), OLED_I2C_TIMEOUT_MS);

    if (e == ESP_OK) {
        buf[0] = 0x40; /* Co=0, D/C#=1:后续字节是显存数据 */
        memcpy(&buf[1], g->fb, GFX_FB_BYTES);
        e = i2c_master_transmit(s_dev, buf, sizeof(buf), OLED_I2C_TIMEOUT_MS);
    }

    uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
    s_stats.last_us = dt;
    if (dt > s_stats.max_us) {
        s_stats.max_us = dt;
    }
    if (e == ESP_OK) {
        s_stats.flush_count++;
    } else {
        s_stats.flush_err++;
    }
    return e;
}

esp_err_t oled_set_on(bool on)
{
    const uint8_t cmd = on ? 0xAF : 0xAE;

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return write_cmds(&cmd, 1);
}

const oled_stats_t *oled_stats(void)
{
    return &s_stats;
}
