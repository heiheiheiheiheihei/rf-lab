#include "led.h"

#include <stdint.h>
#include <string.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define LED_GPIO          48
/* 10MHz => 1 tick = 100ns。WS2812 的容差约 ±150ns,这个分辨率够用。 */
#define LED_RMT_RES_HZ    (10 * 1000 * 1000)
#define LED_T0H_TICKS     4  /* 0.40us */
#define LED_T0L_TICKS     9  /* 0.90us */
#define LED_T1H_TICKS     8  /* 0.80us */
#define LED_T1L_TICKS     5  /* 0.50us */
#define LED_RESET_TICKS   300 /* 每个 30us,两个 = 60us > 50us 复位要求 */

#define LED_TICK_MS       10
#define LED_PERIOD_MS     1000 /* 慢闪 0.5Hz:1s 周期,50% 占空 */
#define LED_ARMED_ON_MS   125  /* 快闪 4Hz:周期 250ms,50% 占空 */
#define LED_LINK_FLASH_MS 50
#define LED_LINK_STEADY_MS 4000

static const char *TAG = "led";

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;
static bool                 s_ready;

/* ws2812_write 的互斥:主任务(例如 KILL 置位时立即写黑)与 LED 任务会并发
 * 调用它,而它内部用的是共享的静态符号缓冲。没有锁时后一次刷新会改写正在
 * 发送的数据(RMT 还在读),表现为颜色错乱。 */
static SemaphoreHandle_t s_lock;

static volatile led_state_t s_state = LED_STATE_KILL;
static volatile uint32_t    s_last_link_ms;
static uint32_t             s_ms;

typedef struct {
    uint8_t r, g, b;
} rgb_t;

/* 亮度刻意压低:指示用,不是照明;同时降低对 3.3V 轨的电流冲击。
 * ⚠️ 颜色是"可选增强",语义必须由 led_pattern_on() 的时序表达。 */
static const rgb_t COL_OFF   = {0, 0, 0};
static const rgb_t COL_GREEN = {0, 48, 0};
static const rgb_t COL_RED   = {96, 0, 0};
static const rgb_t COL_BLUE  = {0, 0, 64};
static const rgb_t COL_PURPLE = {32, 0, 32};

static void ws2812_write(rgb_t c)
{
    if (!s_ready || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);

    /* 24 bit/灯,WS2812 字节序为 G-R-B(高位先出)。 */
    static rmt_symbol_word_t sym[26];
    uint8_t grb[3] = {c.g, c.r, c.b};
    int n = 0;

    for (int byte = 0; byte < 3; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            bool one = (grb[byte] >> bit) & 1;
            sym[n].level0    = 1;
            sym[n].duration0 = one ? LED_T1H_TICKS : LED_T0H_TICKS;
            sym[n].level1    = 0;
            sym[n].duration1 = one ? LED_T1L_TICKS : LED_T0L_TICKS;
            n++;
        }
    }
    /* 复位间隔(低电平 > 50us)。 */
    for (int i = 0; i < 2; i++) {
        sym[n].level0    = 0;
        sym[n].duration0 = LED_RESET_TICKS;
        sym[n].level1    = 0;
        sym[n].duration1 = LED_RESET_TICKS;
        n++;
    }

    rmt_transmit_config_t tx = {.loop_count = 0};
    if (rmt_transmit(s_chan, s_enc, sym, n * sizeof(rmt_symbol_word_t), &tx) == ESP_OK) {
        /* 阻塞到发完再改 sym[],否则下一次刷新会改写正在发送的数据。 */
        rmt_tx_wait_all_done(s_chan, 50);
    }

    xSemaphoreGive(s_lock);
}

void led_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "LED 互斥量创建失败(WS2812 不可用)");
        return;
    }

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num          = LED_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = LED_RMT_RES_HZ,
        /* 96 = 2 × SOC_RMT_MEM_WORDS_PER_CHANNEL(48);我们的单帧 26 个符号。 */
        .mem_block_symbols = 96,
        .trans_queue_depth = 2,
    };
    if (rmt_new_tx_channel(&tx_cfg, &s_chan) != ESP_OK) {
        ESP_LOGE(TAG, "RMT 通道创建失败(WS2812 不可用)");
        return;
    }
    /* ⚠️ IDF 5.5 的 rmt_copy_encoder_config_t 是**空结构体**(没有成员),
     *    `= {0}` 会触发 "excess elements in struct initializer" 警告,
     *    所以这里不做零初始化(没有可取的成员)。 */
    rmt_copy_encoder_config_t enc_cfg;
    if (rmt_new_copy_encoder(&enc_cfg, &s_enc) != ESP_OK) {
        ESP_LOGE(TAG, "RMT copy encoder 创建失败");
        return;
    }
    if (rmt_enable(s_chan) != ESP_OK) {
        ESP_LOGE(TAG, "RMT 使能失败");
        return;
    }
    s_ready = true;

    /* ⚠️ 必须显式写黑:KILL 态要求"灭",而 WS2812 停更会保持上一次颜色。 */
    ws2812_write(COL_OFF);
}

void led_set_state(led_state_t st)
{
    s_state = st;
    if (st == LED_STATE_KILL) {
        /* ⚠️ KILL 必须**立即**写黑,不能只改状态等 LED 任务下一拍(10ms):
         *    - WS2812 停更会保持上一次颜色,这段时间灯可能还是亮的;
         *    - "灯灭"虽不是安全证据,但"灯亮着而系统说 KILL"会直接误导现场判读。
         *    写黑与 LED 任务通过 s_lock 串行化(见 ws2812_write)。 */
        ws2812_write(COL_OFF);
    }
}

void led_note_link_activity(void)
{
    s_last_link_ms = s_ms;
}

/* 该状态在当前时刻是否应为"亮"。语义全在这里,颜色在 led_color_for()。 */
static bool led_pattern_on(led_state_t st)
{
    switch (st) {
    case LED_STATE_KILL:
        return false; /* 常灭 = 安全态 */
    case LED_STATE_IDLE:
        return (s_ms % LED_PERIOD_MS) < (LED_PERIOD_MS / 2);
    case LED_STATE_ARMED:
        return (s_ms % (LED_ARMED_ON_MS * 2)) < LED_ARMED_ON_MS;
    case LED_STATE_TX:
        return true;
    case LED_STATE_FAULT:
        /* 三连急闪(每次 80ms,间隔 120ms)后停 1s。 */
        {
            uint32_t t = s_ms % 1600;
            if (t < 1000) {
                return false;
            }
            uint32_t u = t - 1000; /* 0..600 */
            return (u % 200) < 80;
        }
    default:
        return false;
    }
}

static rgb_t led_color_for(led_state_t st)
{
    switch (st) {
    case LED_STATE_IDLE:  return COL_GREEN;
    case LED_STATE_ARMED: return COL_RED;
    case LED_STATE_TX:    return COL_RED;
    case LED_STATE_FAULT: return COL_PURPLE;
    default:              return COL_OFF;
    }
}

static void led_task(void *arg)
{
    (void)arg;
    rgb_t last = {255, 255, 255}; /* 未知初值,保证第一轮必定写一次 */

    for (;;) {
        led_state_t st = s_state;
        bool on = led_pattern_on(st);
        /* USB 链路叠加:每 4s 一次 50ms 蓝闪。蓝闪**覆盖**当前底色,
         * 因为它回答的是另一个问题("链路还通吗"),不是门控状态。 */
        bool link_flash = (s_ms - s_last_link_ms) < LED_LINK_STEADY_MS &&
                          ((s_ms % LED_LINK_STEADY_MS) < LED_LINK_FLASH_MS);

        rgb_t c = link_flash ? COL_BLUE : (on ? led_color_for(st) : COL_OFF);

        if (c.r != last.r || c.g != last.g || c.b != last.b) {
            ws2812_write(c);
            last = c;
        }

        s_ms += LED_TICK_MS;
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

void led_start(void)
{
    /* 低优先级、独立任务:不让 LED 刷新去挤射频时序(硬件文档 §4)。 */
    xTaskCreate(led_task, "led", 3072, NULL, 4, NULL);
}

void led_selftest(void)
{
    static const rgb_t seq[3] = {{64, 0, 0}, {0, 64, 0}, {0, 0, 64}};

    for (int i = 0; i < 3; i++) {
        ws2812_write(seq[i]);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    /* ⚠️ 自检结束必须**写黑**,不能靠"不写了"—— WS2812 停更会保持最后颜色。 */
    ws2812_write(COL_OFF);
}
