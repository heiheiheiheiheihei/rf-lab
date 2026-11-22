/*
 * RF-Lab 0.5.0 — ESP32-C3 OLED + YS-RF34DL(315/433 编解码模块)
 *
 * 分工:
 *   · 解码、编码、发射全部在模块里完成,ESP32 只做「串口 ↔ 文本命令行」;
 *   · 模块接 UART1(9600 8N1,TX=GPIO3 → 模块 RXD,RX=GPIO4 ← 模块 TXD);
 *   · 命令行走 原生 USB-Serial-JTAG + UART0(GPIO20/21),115200 8N1;
 *   · 板载 OLED(SSD1306 72×40,I2C GPIO5/6)做**纯输出**状态面板:
 *     READY / LEARN / RX / TX 结果 / 计数器;屏不在时静默降级为纯串口;
 *   · 学码 = 按一下原遥控器(模块主动输出 8 字节帧),固件自动入库并打印;
 *   · 码存 NVS,掉电不丢;同码 600ms 内只算一次(模块默认连续输出)。
 *
 * 刻意不做:
 *   · 启动时不向模块发任何配置命令 —— 模块行为未公开,少发一条就少一个假证据源;
 *   · 不解析 EV1527 位结构 —— 模块给的 4 字节原样存、原样回放,不做推断;
 *   · 不把整条串口日志镜像到 OLED —— 72×40 只有 12×5 字符,语义状态页比滚动日志可读。
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app.h"
#include "ble_link.h"
#include "codebook.h"
#include "link.h"
#include "net.h"
#include "oled.h"
#include "oled_gfx.h"
#include "oled_panel.h"
#include "ota.h"
#include "rf34dl.h"
#include "ui.h"
#include "web_server.h"
#include "wiring_config.h"

#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.7.1"
#endif
#ifndef RFLAB_BUILD_ID
#define RFLAB_BUILD_ID "dev"
#endif
#ifndef RFLAB_UI_HASH
#define RFLAB_UI_HASH "unknown"
#endif

/* ---------------- 引脚与参数 ---------------- */
/* 引脚/波特率来自 hardware/wiring.json -> wiring_config.h;勿手改。 */
#define MODULE_UART UART_NUM_1
#define MAX_CODES CODEBOOK_MAX
#define DEDUP_MS 600
/*
 * 发射应答 F0 的实测延迟:0.7–1.0s(2026-11-15,315M,单帧 dur=03)。
 * 原来按 500ms 等,结果每次都先报 WARN、F0 随后才到 —— 属于误报。
 * 给到 1.5s;等待期间仍会处理收到的解码帧。
 */
#define ACK_WAIT_MS 1500

#define NVS_NS "rf34dl"
#define NVS_KEY_V1 "codes"
#define NVS_KEY_V2 "codes2"

/* ---------------- 状态 ---------------- */

static bool s_uart0 = false;
static bool s_usb = false;
static bool s_echo = true;  /* 打印解码事件 */
static bool s_mon = false;  /* 打印模块原始字节 */
static int s_mod_baud = MODULE_BAUD_DEFAULT;

static codebook_t s_codebook;
static codebook_t s_backup;
static uint8_t s_nvs_blob[CODEBOOK_BLOB_MAX];
static uint8_t s_nvs_verify[CODEBOOK_BLOB_MAX];

static rf34dl_parser_t s_parser;

static uint32_t s_rx_bytes = 0;
static uint32_t s_frames = 0;
static uint32_t s_acks = 0;
static uint32_t s_dups = 0;
static uint32_t s_bad_frames = 0;
static int64_t s_bad_note_ms = 0;
static uint8_t s_ack_last = 0;

static uint8_t s_last_band = 0;
static uint8_t s_last_code[RF34DL_CODE_LEN];
static int64_t s_last_ms = 0;

/* ---------------- 跨平台 UI:命令队列 / 快照 / 事件 ---------------- */

#define CMD_QUEUE_DEPTH 8

typedef struct {
    char line[APP_CMD_MAX];
    uint8_t src;
} cmd_item_t;

static QueueHandle_t s_cmd_q;
static SemaphoreHandle_t s_snap_lock;
static char s_state_json[APP_STATE_JSON_MAX];
static char s_codes_json[APP_CODES_JSON_MAX];
static volatile uint32_t s_state_seq = 1;
static volatile uint32_t s_codes_seq = 1;
static volatile bool s_state_dirty = true;
static volatile bool s_codes_dirty = true;

static char s_dev_name[32] = "RF-Lab";
static char s_ble_pin[5] = "0000";

static char s_last_kind[12] = "boot";

/* 网络事件 → OLED/串口:net 任务只入队,main 负责改 ui_model。
 * 缓冲按完整 IPv4 留足(OLED 由 ui_note_boot 的 copy_trunc 再截到 12 列);
 * 以前复用 UI_COLS+1 会在串口把 192.168.5.125 打成 192.168.5.12。 */
#define NET_NOTE_MAX 32
static char s_net_note1[NET_NOTE_MAX];
static char s_net_note2[NET_NOTE_MAX];
static volatile bool s_net_note_pending;
static portMUX_TYPE s_net_note_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_last_id;
static int64_t s_last_event_ms;

/* TX 状态(供状态快照;LONG 时 active=1) */
static bool s_tx_active;
static char s_tx_kind[8] = "idle";
static int s_tx_id;
static int s_tx_ok;
static unsigned s_tx_ms;

/* 命令输出镜像:HTTP/BLE 提交的命令把 out() 聚合为一条 log 事件。 */
static bool s_capture;
static char s_capture_buf[600];
static size_t s_capture_len;

/* OLED 状态面板 */
#define UI_MIN_INTERVAL_MS 100

static ui_model_t s_ui;
static gfx_t s_gfx;
static bool s_oled_ok = false;
static bool s_ui_dirty = true;
static int64_t s_ui_last_draw_ms = 0;
static int64_t s_boot_until_ms = 0;
static int64_t s_test_until_ms = 0;
static bool s_test_drawn = false;
static uint32_t s_oled_err_reported = 0;

/* ---------------- 小工具 ---------------- */

static int64_t now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000);
}

static void hex4(const uint8_t *code, char out[9])
{
    snprintf(out, 9, "%02X%02X%02X%02X", code[0], code[1], code[2], code[3]);
}

static void con_write(const void *data, int len)
{
    if (len <= 0) {
        return;
    }
    if (s_uart0) {
        uart_write_bytes(UART_NUM_0, data, len);
    }
    if (s_usb) {
        usb_serial_jtag_write_bytes(data, (size_t)len, pdMS_TO_TICKS(20));
    }
}

static void out(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        con_write(buf, n);

        if (s_capture && s_capture_len < sizeof(s_capture_buf) - 1) {
            size_t room = sizeof(s_capture_buf) - 1 - s_capture_len;
            size_t take = (size_t)n < room ? (size_t)n : room;
            memcpy(s_capture_buf + s_capture_len, buf, take);
            s_capture_len += take;
            s_capture_buf[s_capture_len] = 0;
        }
    }
}

/* ---------------- 快照 / 命令队列(app.h) ---------------- */

static int jappend(char *buf, size_t cap, int off, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (off < 0 || (size_t)off >= cap) {
        return (int)cap - 1;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf + off, cap - (size_t)off, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return off;
    }
    if ((size_t)n >= cap - (size_t)off) {
        return (int)cap - 1;
    }
    return off + n;
}

static void build_state_json(char *out, size_t cap)
{
    net_info_t ni;
    const oled_stats_t *ost = oled_stats();
    char ap_ssid[96], sta_ssid[96], name[96];
    char hex[9] = "";
    int o = 0;

    net_get_info(&ni);
    link_json_escape(ni.ap_ssid, ap_ssid, sizeof(ap_ssid));
    link_json_escape(ni.sta_ssid, sta_ssid, sizeof(sta_ssid));
    link_json_escape(s_dev_name, name, sizeof(name));
    if (s_last_id > 0) {
        hex4(s_last_code, hex);
    }

    o = jappend(out, cap, o,
                "{\"fw\":\"%s\",\"proto\":1,\"uptime_ms\":%lld,"
                "\"net\":{\"mode\":\"%s\",\"ap_on\":%d,\"has_creds\":%d,"
                "\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\","
                "\"sta_connected\":%s,\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\",\"rssi\":%d},"
                "\"ble\":{\"on\":%d,\"conn\":%d,\"name\":\"%s\",\"pin_set\":1},",
                RFLAB_FW_VERSION, (long long)now_ms(), ni.mode,
                ni.ap_on ? 1 : 0, ni.has_creds ? 1 : 0, ap_ssid, ni.ap_ip,
                ni.sta_connected ? "true" : "false", sta_ssid, ni.sta_ip, ni.rssi,
                ble_link_is_on() ? 1 : 0, ble_link_connected() ? 1 : 0, name);
    o = jappend(out, cap, o,
                "\"module\":{\"baud\":%d,\"rx_bytes\":%lu,\"frames\":%lu,\"acks\":%lu,"
                "\"dups\":%lu,\"bad\":%lu,\"stray\":%lu,\"resync\":%lu},"
                "\"store\":{\"count\":%d,\"max\":%d,\"ver\":%d},"
                "\"oled\":{\"present\":%d,\"on\":%d,\"flush\":%lu,\"err\":%lu,"
                "\"last_us\":%lu,\"max_us\":%lu},",
                s_mod_baud, (unsigned long)s_rx_bytes, (unsigned long)s_frames,
                (unsigned long)s_acks, (unsigned long)s_dups, (unsigned long)s_bad_frames,
                (unsigned long)s_parser.stray, (unsigned long)s_parser.dropped,
                s_codebook.count, MAX_CODES, CODEBOOK_VERSION,
                s_oled_ok ? 1 : 0, s_ui.enabled ? 1 : 0,
                ost ? (unsigned long)ost->flush_count : 0UL,
                ost ? (unsigned long)ost->flush_err : 0UL,
                ost ? (unsigned long)ost->last_us : 0UL,
                ost ? (unsigned long)ost->max_us : 0UL);
    o = jappend(out, cap, o,
                "\"ota\":{\"armed\":%d,\"pct\":%d,\"phase\":\"%s\",\"slot\":%d,"
                "\"next\":%d,\"pending\":%d},",
                ota_is_armed() ? 1 : 0, ota_progress(), ota_phase(),
                ota_running_slot(), ota_target_slot(), ota_pending_verify() ? 1 : 0);
    o = jappend(out, cap, o,
                "\"tx\":{\"active\":%d,\"kind\":\"%s\",\"id\":%d,\"ok\":%d,\"ms\":%u},"
                "\"last\":{\"kind\":\"%s\",\"id\":%d,\"band\":\"%s\",\"code\":\"%s\","
                "\"ms\":%lld}}",
                s_tx_active ? 1 : 0, s_tx_kind, s_tx_id, s_tx_ok, s_tx_ms,
                s_last_kind, s_last_id,
                s_last_id > 0 ? rf34dl_band_name(s_last_band) : "",
                hex, (long long)s_last_event_ms);
    out[o] = 0;
}

static void build_codes_json(char *out, size_t cap)
{
    codebook_codes_json(&s_codebook, out, cap);
}

static void snapshot_task(void *arg)
{
    (void)arg;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_state_dirty) {
            xSemaphoreTake(s_snap_lock, portMAX_DELAY);
            if (s_state_dirty) {
                build_state_json(s_state_json, sizeof(s_state_json));
                s_state_seq++;
                s_state_dirty = false;
            }
            xSemaphoreGive(s_snap_lock);
        }
        if (s_codes_dirty) {
            xSemaphoreTake(s_snap_lock, portMAX_DELAY);
            if (s_codes_dirty) {
                build_codes_json(s_codes_json, sizeof(s_codes_json));
                s_codes_seq++;
                s_codes_dirty = false;
            }
            xSemaphoreGive(s_snap_lock);
        }
    }
}

void app_note_net_event(const char *line1, const char *line2)
{
    portENTER_CRITICAL(&s_net_note_mux);
    snprintf(s_net_note1, sizeof(s_net_note1), "%s", line1 ? line1 : "");
    snprintf(s_net_note2, sizeof(s_net_note2), "%s", line2 ? line2 : "");
    s_net_note_pending = true;
    portEXIT_CRITICAL(&s_net_note_mux);
}

static void ui_mark_dirty(void);

static void poll_net_note(void)
{
    if (!s_net_note_pending) {
        return;
    }
    char l1[NET_NOTE_MAX];
    char l2[NET_NOTE_MAX];
    portENTER_CRITICAL(&s_net_note_mux);
    snprintf(l1, sizeof(l1), "%s", s_net_note1);
    snprintf(l2, sizeof(l2), "%s", s_net_note2);
    s_net_note_pending = false;
    portEXIT_CRITICAL(&s_net_note_mux);

    out("# net %s %s\n", l1, l2);
    ui_note_boot(&s_ui, l1, l2);
    s_boot_until_ms = now_ms() + 5000;
    ui_mark_dirty();
}

void app_mark_state_dirty(void)
{
    s_state_dirty = true;
}

void app_mark_codes_dirty(void)
{
    s_codes_dirty = true;
    s_state_dirty = true;
}

uint32_t app_state_seq(void)
{
    return __atomic_load_n(&s_state_seq, __ATOMIC_ACQUIRE);
}

uint32_t app_codes_seq(void)
{
    return __atomic_load_n(&s_codes_seq, __ATOMIC_ACQUIRE);
}

static void ensure_snapshots(void)
{
    if (!s_snap_lock) {
        return;
    }
    xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    if (s_state_json[0] == 0) {
        build_state_json(s_state_json, sizeof(s_state_json));
    }
    if (s_codes_json[0] == 0) {
        build_codes_json(s_codes_json, sizeof(s_codes_json));
    }
    xSemaphoreGive(s_snap_lock);
}

void app_copy_state(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    if (!s_snap_lock) {
        return;
    }
    ensure_snapshots();
    xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    snprintf(out, cap, "%s", s_state_json);
    xSemaphoreGive(s_snap_lock);
}

void app_copy_codes(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    if (!s_snap_lock) {
        return;
    }
    ensure_snapshots();
    xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    snprintf(out, cap, "%s", s_codes_json);
    xSemaphoreGive(s_snap_lock);
}

int app_submit_cmd(const char *line, app_src_t src)
{
    cmd_item_t it;

    if (!line || !s_cmd_q) {
        return -1;
    }
    memset(&it, 0, sizeof(it));
    snprintf(it.line, sizeof(it.line), "%s", line);
    it.src = (uint8_t)src;

    size_t n = strlen(it.line);
    while (n > 0 && (it.line[n - 1] == '\r' || it.line[n - 1] == '\n')) {
        it.line[--n] = 0;
    }
    if (n == 0) {
        return 0;
    }
    return xQueueSend(s_cmd_q, &it, 0) == pdPASS ? 0 : -1;
}

const char *app_name(void)
{
    return s_dev_name;
}

const char *app_ble_pin(void)
{
    return s_ble_pin;
}

bool app_ble_enabled(void)
{
    return ble_link_is_on();
}

int64_t app_now_ms(void)
{
    return now_ms();
}

/* ---------------- OLED 状态面板 ---------------- */

static void ui_mark_dirty(void)
{
    s_ui_dirty = true;
}

static void oled_report_err(const char *what, esp_err_t e)
{
    const oled_stats_t *st = oled_stats();
    uint32_t n = st ? st->flush_err : 0;

    /* 只在第一次和每 25 次失败时打印,避免连续帧把控制台刷满。 */
    if (n == 1 || n - s_oled_err_reported >= 25) {
        s_oled_err_reported = n;
        out("# WARN oled %s: %s (flush_err=%lu)\n", what, esp_err_to_name(e),
            (unsigned long)n);
    }
}

/* 把模型渲染进 s_gfx(纯内存,不碰 I2C);DISP SHOW 也用它。 */
static void ui_draw_model(void)
{
    char rows[UI_ROWS][UI_COLS + 1];

    ui_render(&s_ui, rows);
    gfx_clear(&s_gfx);
    for (int r = 0; r < UI_ROWS; r++) {
        gfx_text_row(&s_gfx, r, rows[r]);
    }
}

/*
 * 把当前状态画到屏上。只在"脏"或 force 时真正走 I2C,并且全帧刷新最多 10Hz:
 * 整帧 361 字节 @400kHz 实测 ≈8.6ms,不能让它挤占模块 F0(0.85s)的接收窗口。
 */
static void ui_refresh(bool force)
{
    int64_t now = now_ms();

    if (s_test_until_ms && now >= s_test_until_ms) {
        s_test_until_ms = 0;
        s_test_drawn = false;
        s_ui_dirty = true;
    }
    if (s_boot_until_ms && now >= s_boot_until_ms) {
        s_boot_until_ms = 0;
        if (s_ui.ev == UI_EV_BOOT) {
            ui_note_idle(&s_ui);
        }
        s_ui_dirty = true;
    }

    if (!s_oled_ok || !s_ui.enabled) {
        s_ui_dirty = false;
        return;
    }
    if (s_test_until_ms) {
        if (force || !s_test_drawn) {
            ui_test_pattern(&s_gfx);
            esp_err_t e = oled_flush(&s_gfx);
            if (e != ESP_OK) {
                oled_report_err("test", e);
            } else {
                s_test_drawn = true;
            }
            s_ui_last_draw_ms = now;
        }
        return;
    }
    if (!s_ui_dirty && !force) {
        return;
    }
    if (!force && now - s_ui_last_draw_ms < UI_MIN_INTERVAL_MS) {
        return; /* 保留 dirty,下一轮再画 */
    }

    ui_draw_model();

    esp_err_t e = oled_flush(&s_gfx);
    if (e != ESP_OK) {
        oled_report_err("flush", e);
    }
    s_ui_last_draw_ms = now;
    s_ui_dirty = false;
}

/* ---------------- 初始化 ---------------- */

static void init_console(void)
{
    uart_config_t cfg = {
        .baud_rate = CONSOLE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(UART_NUM_0, 1024, 2048, 0, NULL, 0) == ESP_OK) {
        uart_param_config(UART_NUM_0, &cfg);
        uart_set_pin(UART_NUM_0, PIN_CONSOLE_TX, PIN_CONSOLE_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        s_uart0 = true;
    }

    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ucfg.rx_buffer_size = 1024;
    ucfg.tx_buffer_size = 2048;
    s_usb = (usb_serial_jtag_driver_install(&ucfg) == ESP_OK);
}

static void init_module(void)
{
    uart_config_t cfg = {
        .baud_rate = s_mod_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(MODULE_UART, 2048, 1024, 0, NULL, 0);
    uart_param_config(MODULE_UART, &cfg);
    uart_set_pin(MODULE_UART, PIN_MODULE_TX, PIN_MODULE_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    /* 模块未上电/未接线时 RX 悬空,上拉让它读到空闲高而不是随机毛刺 */
    gpio_set_pull_mode(PIN_MODULE_RX, GPIO_PULLUP_ONLY);
    rf34dl_parser_init(&s_parser);
}

/* ---------------- NVS ---------------- */

static bool s_store_migrated = false;

static bool store_write(const codebook_t *cb)
{
    size_t n = codebook_serialize(cb, s_nvs_blob, sizeof(s_nvs_blob));
    if (n == 0) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e = nvs_set_blob(h, NVS_KEY_V2, s_nvs_blob, n);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    return e == ESP_OK;
}

/* 1 = 读到合法 v2;0 = key 不存在; -1 = 存在但损坏(不能拿旧 v1 覆盖)。 */
static int store_read_v2(codebook_t *out)
{
    nvs_handle_t h;
    size_t len = sizeof(s_nvs_blob);

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    esp_err_t e = nvs_get_blob(h, NVS_KEY_V2, s_nvs_blob, &len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (e != ESP_OK || len != sizeof(codebook_blob_t)) {
        return -1;
    }
    return codebook_deserialize(out, s_nvs_blob, len) == CODEBOOK_OK ? 1 : -1;
}

/* 读 v1(164B),迁移到 v2 新 key;写后读回校验,失败仍保留 v1 原值。 */
static bool store_migrate_v1(void)
{
    uint8_t v1[4 + CODEBOOK_MAX * 5];
    nvs_handle_t h;
    size_t len = sizeof(v1);

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e = nvs_get_blob(h, NVS_KEY_V1, v1, &len);
    nvs_close(h);
    if (e != ESP_OK) {
        return false;
    }

    codebook_t tmp;
    codebook_err_t ce = codebook_migrate_v1(&tmp, v1, len);
    if (ce != CODEBOOK_OK) {
        out("# codebook v1 ignored (%s)\n", codebook_err_name(ce));
        return false;
    }
    if (!store_write(&tmp)) {
        out("# codebook v1 found but v2 write failed; keeping v1 only\n");
        s_codebook = tmp; /* 内存里可用,但重启会回到 v1 */
        return true;
    }

    /* 读回校验:不信任 nvs_set_blob 的返回值,防止写半截。 */
    size_t vlen = sizeof(s_nvs_verify);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        e = nvs_get_blob(h, NVS_KEY_V2, s_nvs_verify, &vlen);
        nvs_close(h);
    } else {
        e = ESP_FAIL;
    }
    if (e != ESP_OK || vlen != sizeof(codebook_blob_t) ||
        memcmp(s_nvs_blob, s_nvs_verify, vlen) != 0) {
        out("# codebook migration verify failed; v1 kept, in-memory only\n");
        s_codebook = tmp;
        return true;
    }

    s_codebook = tmp;
    s_store_migrated = true;
    out("# codebook migrated v1 -> v2 (%d code(s))\n", s_codebook.count);
    return true;
}

static bool store_load(void)
{
    codebook_init(&s_codebook);
    int v2 = store_read_v2(&s_codebook);
    if (v2 == 1) {
        return true;
    }
    if (v2 == -1) {
        /* v2 存在但损坏:不要用过期 v1 覆盖它;保留现场供串口排查。 */
        out("# codebook v2 corrupt; keeping it, codes unavailable this boot\n");
        codebook_init(&s_codebook);
        return false;
    }
    if (store_migrate_v1()) {
        return true;
    }
    codebook_init(&s_codebook);
    return false;
}

static void store_backup(void)
{
    s_backup = s_codebook;
}

static bool store_commit(void)
{
    if (store_write(&s_codebook)) {
        return true;
    }
    s_codebook = s_backup;
    out("ERR nvs write failed; change rolled back\n");
    return false;
}

static bool init_store(void)
{
    esp_err_t e = nvs_flash_init();

    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        out("# NVS init failed (%s): codes will not persist\n", esp_err_to_name(e));
        return false;
    }
    store_load(); /* 空库/格式不符都返回 false,这里只关心 NVS 是否可用 */
    return true;
}

/* ---------------- 模块收发 ---------------- */

static size_t module_write(const uint8_t *d, size_t n)
{
    int w = uart_write_bytes(MODULE_UART, d, n);

    if (s_mon && w > 0) {
        out("MON TX");
        for (int i = 0; i < w; i++) {
            out(" %02X", d[i]);
        }
        out("\n");
    }
    return (size_t)(w < 0 ? 0 : w);
}

static void handle_ack(uint8_t a);

static void handle_frame_locked(const rf34dl_frame_t *f)
{
    int64_t now = now_ms();
    char hex[9];

    hex4(f->code, hex);

    /* 模块默认「连续输出」:按住键会连发同一帧,600ms 内只算一次 */
    if (f->band == s_last_band &&
        memcmp(f->code, s_last_code, RF34DL_CODE_LEN) == 0 &&
        now - s_last_ms < DEDUP_MS) {
        s_dups++;
        s_last_ms = now;
        return;
    }
    /* 模块协议:地址1/地址2/按键 0x01..0xFF,震荡参数 0x30..0xF0。
     * 越界帧(如旧日志里的 000000xx)不学、不占码库,单独计数。 */
    if (!codebook_code_valid(f->band, f->code)) {
        s_bad_frames++;
        s_last_event_ms = now;
        /* 噪声可能持续刷同一种坏帧:计数每次加,串口提示最多 1 秒一条。 */
        if (s_echo && now - s_bad_note_ms >= 1000) {
            s_bad_note_ms = now;
            out("RX band=%s code=%s (bad frame: addr/button 0 or osc outside 0x30..0xF0)\n",
                rf34dl_band_name(f->band), hex);
        }
        return;
    }

    s_last_ms = now;
    s_last_band = f->band;
    memcpy(s_last_code, f->code, RF34DL_CODE_LEN);

    int idx = codebook_find(&s_codebook, f->band, f->code);
    if (idx >= 0) {
        snprintf(s_last_kind, sizeof(s_last_kind), "rx");
        s_last_id = idx + 1;
        s_last_event_ms = now;
        ui_note_rx(&s_ui, idx + 1, f->band, f->code);
        ui_mark_dirty();
        app_mark_state_dirty();
        link_publishf("{\"t\":\"event\",\"seq\":%lu,\"kind\":\"rx\",\"id\":%d,"
                      "\"band\":\"%s\",\"code\":\"%s\"}",
                      (unsigned long)(link_head() + 1), idx + 1,
                      rf34dl_band_name(f->band), hex);
        if (s_echo) {
            out("RX id=%d band=%s code=%s\n", idx + 1, rf34dl_band_name(f->band), hex);
        }
        return;
    }

    if (s_codebook.count >= MAX_CODES) {
        snprintf(s_last_kind, sizeof(s_last_kind), "full");
        s_last_id = 0;
        s_last_event_ms = now;
        ui_note_full(&s_ui, f->band, f->code);
        ui_mark_dirty();
        app_mark_state_dirty();
        link_publishf("{\"t\":\"event\",\"seq\":%lu,\"kind\":\"full\","
                      "\"band\":\"%s\",\"code\":\"%s\"}",
                      (unsigned long)(link_head() + 1), rf34dl_band_name(f->band), hex);
        if (s_echo) {
            out("RX band=%s code=%s (store full, not saved)\n", rf34dl_band_name(f->band), hex);
        }
        return;
    }

    int new_id = 0;
    store_backup();
    codebook_err_t ce = codebook_add(&s_codebook, f->band, f->code, NULL, false, &new_id);
    if (ce != CODEBOOK_OK || !store_commit()) {
        if (ce != CODEBOOK_OK) {
            out("ERR learn store: %s\n", codebook_err_name(ce));
        }
        return;
    }

    snprintf(s_last_kind, sizeof(s_last_kind), "learn");
    s_last_id = new_id;
    s_last_event_ms = now;
    ui_note_learn(&s_ui, new_id, f->band, f->code);
    ui_note_count(&s_ui, s_codebook.count);
    ui_mark_dirty();
    app_mark_codes_dirty();
    link_publishf("{\"t\":\"event\",\"seq\":%lu,\"kind\":\"learn\",\"id\":%d,"
                  "\"band\":\"%s\",\"code\":\"%s\"}",
                  (unsigned long)(link_head() + 1), new_id,
                  rf34dl_band_name(f->band), hex);

    if (s_echo) {
        out("LEARN id=%d band=%s code=%s\n", new_id, rf34dl_band_name(f->band), hex);
    }
}

static void handle_frame(const rf34dl_frame_t *f)
{
    if (s_snap_lock) {
        xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    }
    handle_frame_locked(f);
    if (s_snap_lock) {
        xSemaphoreGive(s_snap_lock);
    }
}

static void handle_ack(uint8_t a)
{
    const char *name = "unknown";
    uint8_t prev = s_ack_last;

    s_ack_last = a;
    switch (a) {
    case RF34DL_ACK_TX_OK:
        name = "TX_OK";
        break;
    case RF34DL_ACK_BAUD_OK:
        name = "BAUD_OK";
        break;
    case RF34DL_ACK_SINGLE_OK:
        name = "OUTPUT_SINGLE_OK";
        break;
    case RF34DL_ACK_CONT_OK:
        name = "OUTPUT_CONT_OK";
        break;
    case RF34DL_ACK_LONG_ON:
        name = "LONG_ON";
        break;
    case RF34DL_ACK_STOPPED:
        name = "STOPPED";
        break;
    default:
        break;
    }
    /*
     * 长发期间 0xFE 约 25Hz 重复(实测),逐条打印会把控制台刷满;
     * 只在状态变化时打印一次,后续心跳仍计入 acks(INFO 可见)。
     */
    if (s_echo && !((a == RF34DL_ACK_LONG_ON || a == RF34DL_ACK_STOPPED) && a == prev)) {
        out("ACK 0x%02X %s\n", a, name);
    }
    app_mark_state_dirty();
}

static int pump(int wait_ms)
{
    int64_t t0 = now_ms();
    int total = 0;

    do {
        uint8_t tmp[64];
        int r = uart_read_bytes(MODULE_UART, tmp, sizeof(tmp), pdMS_TO_TICKS(5));

        for (int i = 0; i < r; i++) {
            s_rx_bytes++;
            if (s_mon) {
                out("MON RX %02X\n", tmp[i]);
            }
            rf34dl_ev_t ev = rf34dl_parser_feed(&s_parser, tmp[i]);
            if (ev == RF34DL_EV_FRAME) {
                s_frames++;
                handle_frame(&s_parser.frame);
            } else if (ev == RF34DL_EV_ACK) {
                s_acks++;
                handle_ack(s_parser.ack);
            }
        }
        total += r;
    } while (now_ms() - t0 < wait_ms);

    if (total > 0) {
        ui_note_counters(&s_ui, (unsigned)s_frames, (unsigned)s_acks,
                         (unsigned)s_parser.stray);
        ui_mark_dirty();
        app_mark_state_dirty();
    }
    ui_refresh(false);
    return total;
}

/* 等一个特定应答;返回最后收到的应答(0 = 没收到) */
static uint8_t wait_ack(uint8_t want, int ms)
{
    int64_t t0 = now_ms();

    s_ack_last = 0;
    while (now_ms() - t0 < ms) {
        pump(10);
        if (s_ack_last == want) {
            return want;
        }
    }
    return s_ack_last;
}

/* ---------------- 命令 ---------------- */

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int parse_hex(const char *s, uint8_t *dst, int max)
{
    int n = 0;
    int hi = -1;

    for (; *s; s++) {
        int v = hex_val(*s);
        if (v < 0) {
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else {
            if (n < max) {
                dst[n++] = (uint8_t)((hi << 4) | v);
            }
            hi = -1;
        }
    }
    return (hi >= 0) ? -1 : n;
}

/* 参数比较:大小写不敏感,避免 "MODE Single" 这种输入被判错 */
static bool arg_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

/* 前缀比较(大小写不敏感),用于 "DISP TEST 20" 这种带参数的子命令。 */
static bool arg_prefix(const char *a, const char *b)
{
    while (*b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return true;
}

static void cmd_help(void)
{
    out("commands (115200 8N1):\n");
    out("  HELP                  this list\n");
    out("  INFO                  fw / module baud / counters\n");
    out("  LIST                  stored codes: id band code\n");
    out("  LIST LONG             id band code name group sym flags\n");
    out("  SEND <id>             transmit once (frame dur=03)\n");
    out("  LONG <id>             start continuous transmit (dur=FE)\n");
    out("  STOP <id>             stop continuous transmit (dur=FF)\n");
    out("  ERASE <id|all>        delete one code / all codes (ids shift)\n");
    out("  DEDUP                 merge records with same band+addr+button (osc differs)\n");
    out("  NEW <band> <hex> [name]  add a manual code (315/433, 8 hex)\n");
    out("  NAME <id> [text]      set/clear UTF-8 name (max 24 bytes)\n");
    out("  SYM <id> [text]       set/clear symbol (max 8 bytes)\n");
    out("  GROUP <id> [text]     set/clear single group (max 16 bytes)\n");
    out("  EDIT <id> band|code <v>  edit raw band / 4-byte payload\n");
    out("  MODE single|continuous  module decode output mode\n");
    out("  BAUD <n>              module baud 4800/9600/57600/115200\n");
    out("  RECOVER               scan module baud and force it back to 9600\n");
    out("  RAW <hex>             send raw bytes to module (debug)\n");
    out("  MON ON|OFF            hex-dump every module byte (debug)\n");
    out("  ECHO ON|OFF           print decoded frames (default ON)\n");
    out("  DISP ON|OFF|TEST [s]|SHOW  OLED panel / test pattern / ASCII dump\n");
    out("  WIFI [status]|[ssid pass]|CLEAR  join an AP / forget STA (AP fallback)\n");
    out("  BLE [on|off]          BLE advertising / status\n");
    out("  PIN                   show BLE 4-digit PIN (serial only)\n");
    out("  OTA | OTA OFF         arm firmware upload (token on serial/OLED, 10 min)\n");
    out("  STATE                 print JSON state snapshot\n");
    out("  WEBTEST               self-check HTTP /api/hello + /api/events\n");
    out("  RESET                 restart ESP32\n");
    out("\ncode = addr2 addr1 button osc (8 hex); osc 0x30..0xF0 (default 0x60)\n");
    out("identity = band + addr2 + addr1 + button; osc is NOT part of identity\n");
    out("\nlearn: press a remote button; firmware prints LEARN id=... automatically\n");
    out("note: SEND gets F0 after ~0.85s; LONG gets 0xFE heartbeat; STOP gets 0xFF (measured)\n");
}

static void cmd_info(void)
{
    out("# RF-Lab fw=%s (ESP32-C3 + YS-RF34DL)\n", RFLAB_FW_VERSION);
    out("# build=%s ui=%s\n", RFLAB_BUILD_ID, RFLAB_UI_HASH);
    out("# ota slot=%d next=%d state=%s pct=%d pending=%d\n",
        ota_running_slot(), ota_target_slot(), ota_phase(), ota_progress(),
        ota_pending_verify() ? 1 : 0);
    out("# module UART1 %d 8N1 TX=GPIO%d RX=GPIO%d\n", s_mod_baud, PIN_MODULE_TX, PIN_MODULE_RX);
    out("# console uart0:%s usb:%s %d 8N1\n", s_uart0 ? "on" : "off", s_usb ? "on" : "off",
        CONSOLE_BAUD);
    const oled_stats_t *st = oled_stats();
    out("# oled 72x40 @0x%02X i2c%d sda=%d scl=%d present=%s on=%s flush=%lu err=%lu "
        "last_us=%lu max_us=%lu\n",
        OLED_PANEL_ADDR, oled_bus_hz(), PIN_OLED_SDA, PIN_OLED_SCL,
        s_oled_ok ? "yes" : "no", s_ui.enabled ? "yes" : "no",
        (unsigned long)st->flush_count, (unsigned long)st->flush_err,
        (unsigned long)st->last_us, (unsigned long)st->max_us);
    net_info_t ni;
    net_get_info(&ni);
    out("# net mode=%s ap_on=%d has_creds=%d ap=%s ap_ip=%s clients=%d "
        "sta=%s sta_ip=%s rssi=%d\n",
        ni.mode, ni.ap_on ? 1 : 0, ni.has_creds ? 1 : 0,
        ni.ap_ssid, ni.ap_ip, ni.ap_clients,
        ni.sta_connected ? ni.sta_ssid : "-", ni.sta_ip, ni.rssi);
    out("# ble on=%s adv=%s conn=%s name=%s pin=set\n", ble_link_is_on() ? "on" : "off",
        ble_link_advertising() ? "yes" : "no", ble_link_connected() ? "yes" : "no", s_dev_name);
    out("# codebook ver=%d count=%d/%d migrated=%s\n", CODEBOOK_VERSION,
        s_codebook.count, CODEBOOK_MAX, s_store_migrated ? "yes" : "no");
    out("OK stored=%d/%d rx_bytes=%lu frames=%lu acks=%lu dups=%lu bad=%lu stray=%lu resync=%lu\n",
        s_codebook.count, MAX_CODES, (unsigned long)s_rx_bytes, (unsigned long)s_frames,
        (unsigned long)s_acks, (unsigned long)s_dups, (unsigned long)s_bad_frames,
        (unsigned long)s_parser.stray, (unsigned long)s_parser.dropped);
}

static void cmd_list(void)
{
    if (s_codebook.count == 0) {
        out("(empty; press a remote button and watch for LEARN)\n");
        return;
    }
    for (int i = 0; i < s_codebook.count; i++) {
        char hex[9];
        hex4(s_codebook.recs[i].code, hex);
        out("%2d  %-4s %s\n", i + 1, rf34dl_band_name(s_codebook.recs[i].band), hex);
    }
}

static char *skip_ws(char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static char *trim_ws(char *p)
{
    p = skip_ws(p);
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) {
        p[--n] = 0;
    }
    return p;
}

/* 取一个空格分隔的 token;过长直接失败(避免截断后被当成合法 id/hex)。 */
static bool take_token(char **p, char *tok, size_t cap)
{
    char *s = skip_ws(*p);
    if (*s == 0) {
        *p = s;
        return false;
    }
    size_t n = 0;
    while (*s && *s != ' ' && *s != '\t') {
        if (n + 1 >= cap) {
            while (*s && *s != ' ' && *s != '\t') {
                s++;
            }
            *p = s;
            return false;
        }
        tok[n++] = *s++;
    }
    tok[n] = 0;
    *p = s;
    return true;
}

static bool parse_id_token(const char *s, int *out)
{
    if (!s || !*s) {
        return false;
    }
    int v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        v = v * 10 + (*p - '0');
        if (v > 9999) {
            return false;
        }
    }
    if (v < 1) {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_band_token(const char *s, uint8_t *band)
{
    if (arg_eq(s, "315") || arg_eq(s, "315M")) {
        *band = RF34DL_BAND_315;
        return true;
    }
    if (arg_eq(s, "433") || arg_eq(s, "433M")) {
        *band = RF34DL_BAND_433;
        return true;
    }
    return false;
}

static bool parse_code8(const char *s, uint8_t code[RF34DL_CODE_LEN])
{
    if (!s || strlen(s) != 8) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        int hi = hex_val(s[i * 2]);
        int lo = hex_val(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        code[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void snap_take(void)
{
    if (s_snap_lock) {
        xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    }
}

static void snap_give(void)
{
    if (s_snap_lock) {
        xSemaphoreGive(s_snap_lock);
    }
}

static void cmd_dedup(void)
{
    snap_take();
    store_backup();
    int removed = 0;
    for (int i = 0; i < s_codebook.count;) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (codebook_same_identity(&s_codebook.recs[j], &s_codebook.recs[i])) {
                dup = true;
                break;
            }
        }
        if (dup) {
            codebook_erase(&s_codebook, i + 1);
            removed++;
        } else {
            i++;
        }
    }
    if (removed > 0) {
        if (!store_commit()) {
            snap_give();
            return;
        }
        ui_note_count(&s_ui, s_codebook.count);
        app_mark_codes_dirty();
    }
    snap_give();
    out("OK dedup removed=%d count=%d\n", removed, s_codebook.count);
}

static void cmd_list_long(void)
{
    if (s_codebook.count == 0) {
        out("(empty; press a remote button and watch for LEARN)\n");
        return;
    }
    for (int i = 0; i < s_codebook.count; i++) {
        const codebook_rec_t *r = &s_codebook.recs[i];
        char hex[9];
        hex4(r->code, hex);
        out("%2d  %-4s %s osc=0x%02X  name=\"%s\" group=\"%s\" sym=\"%s\"%s%s\n",
            i + 1, rf34dl_band_name(r->band), hex, r->code[3],
            r->name[0] ? r->name : "-", r->group[0] ? r->group : "-",
            r->sym[0] ? r->sym : "-",
            (r->flags & CODEBOOK_FLAG_MANUAL) ? " manual" : "",
            (r->flags & CODEBOOK_FLAG_EDITED) ? " edited" : "");
    }
}

static void cmd_set_text(const char *args, int field)
{
    char *p = (char *)args;
    char tok[16];
    int id = 0;
    if (!take_token(&p, tok, sizeof(tok)) || !parse_id_token(tok, &id)) {
        out("ERR usage: <NAME|SYM|GROUP> <id> [text]\n");
        return;
    }
    char *text = trim_ws(p);
    snap_take();
    store_backup();
    codebook_err_t e = field == 0 ? codebook_set_name(&s_codebook, id, text)
                       : field == 1 ? codebook_set_sym(&s_codebook, id, text)
                                    : codebook_set_group(&s_codebook, id, text);
    if (e != CODEBOOK_OK) {
        snap_give();
        out("ERR %s: %s (id=%d)\n",
            field == 0 ? "name" : field == 1 ? "sym" : "group",
            codebook_err_name(e), id);
        return;
    }
    if (!store_commit()) {
        snap_give();
        return;
    }
    app_mark_codes_dirty();
    snap_give();
    out("OK %s id=%d\n", field == 0 ? "name" : field == 1 ? "sym" : "group", id);
}

static void cmd_new(const char *args)
{
    char *p = (char *)args;
    char band_tok[16];
    char code_tok[16];
    uint8_t band = 0;
    uint8_t code[RF34DL_CODE_LEN];
    int id = 0;

    if (!take_token(&p, band_tok, sizeof(band_tok)) ||
        !parse_band_token(band_tok, &band) ||
        !take_token(&p, code_tok, sizeof(code_tok)) ||
        !parse_code8(code_tok, code)) {
        out("ERR usage: NEW <315|433> <8 hex> [name]\n");
        return;
    }
    char *name = trim_ws(p);
    snap_take();
    store_backup();
    codebook_err_t e = codebook_add(&s_codebook, band, code, name, true, &id);
    if (e != CODEBOOK_OK) {
        snap_give();
        if (e == CODEBOOK_ERR_DUP) {
            out("ERR new: dup (same band+addr+button; EDIT <id> code <8hex> to change osc)\n");
        } else {
            out("ERR new: %s\n", codebook_err_name(e));
        }
        return;
    }
    if (!store_commit()) {
        snap_give();
        return;
    }
    ui_note_count(&s_ui, s_codebook.count);
    app_mark_codes_dirty();
    snap_give();
    out("OK new id=%d band=%s code=%s\n", id, codebook_band_name(band), code_tok);
}

static void cmd_edit(const char *args)
{
    char *p = (char *)args;
    char id_tok[16];
    char field_tok[16];
    char value_tok[16];
    int id = 0;

    if (!take_token(&p, id_tok, sizeof(id_tok)) || !parse_id_token(id_tok, &id) ||
        !take_token(&p, field_tok, sizeof(field_tok)) ||
        !take_token(&p, value_tok, sizeof(value_tok))) {
        out("ERR usage: EDIT <id> band|code <value>\n");
        return;
    }
    if (s_tx_active) {
        out("ERR tx active; STOP first\n");
        return;
    }

    snap_take();
    store_backup();
    codebook_err_t e;
    if (arg_eq(field_tok, "band")) {
        uint8_t band = 0;
        if (!parse_band_token(value_tok, &band)) {
            snap_give();
            out("ERR band must be 315/433\n");
            return;
        }
        e = codebook_set_band(&s_codebook, id, band);
    } else if (arg_eq(field_tok, "code")) {
        uint8_t code[RF34DL_CODE_LEN];
        if (!parse_code8(value_tok, code)) {
            snap_give();
            out("ERR code must be 8 hex digits\n");
            return;
        }
        e = codebook_set_code(&s_codebook, id, code);
    } else {
        snap_give();
        out("ERR field must be band|code\n");
        return;
    }
    if (e != CODEBOOK_OK) {
        snap_give();
        if (e == CODEBOOK_ERR_DUP) {
            out("ERR edit: dup (another record has the same band+addr+button)\n");
        } else {
            out("ERR edit: %s (id=%d)\n", codebook_err_name(e), id);
        }
        return;
    }
    if (!store_commit()) {
        snap_give();
        return;
    }
    app_mark_codes_dirty();
    snap_give();
    out("OK edit id=%d %s=%s\n", id, field_tok, value_tok);
}

static void do_tx(const char *args, uint8_t dur, const char *what)
{
    int id = atoi(args);
    uint8_t f[RF34DL_FRAME_LEN];
    char hex[9];

    if (id < 1 || id > s_codebook.count) {
        out("ERR id must be 1..%d (LIST)\n", s_codebook.count);
        return;
    }

    const codebook_rec_t *c = codebook_get(&s_codebook, id);
    hex4(c->code, hex);
    if (rf34dl_build_tx(f, c->band, dur, c->code) != RF34DL_FRAME_LEN) {
        out("ERR build failed\n");
        return;
    }

    ui_tx_kind_t kind = (dur == RF34DL_DUR_LONG)   ? UI_TX_LONG
                        : (dur == RF34DL_DUR_STOP) ? UI_TX_STOP
                                                   : UI_TX_SEND;
    snprintf(s_tx_kind, sizeof(s_tx_kind), "%s", what);
    s_tx_id = id;
    s_tx_active = true;
    s_tx_ok = 0;
    s_tx_ms = 0;
    app_mark_state_dirty();
    ui_note_tx(&s_ui, kind, id, c->band, c->code);
    ui_mark_dirty();
    ui_refresh(true); /* 先把 sending... 画出来,再进入最长 1.5s 的等待 */

    /* 单次发射等 F0;长发等 0xFE 心跳;停止等 0xFF(2026-11-15 实测)。 */
    uint8_t need = (dur == RF34DL_DUR_LONG)   ? RF34DL_ACK_LONG_ON
                   : (dur == RF34DL_DUR_STOP) ? RF34DL_ACK_STOPPED
                                              : RF34DL_ACK_TX_OK;
    int64_t t0 = now_ms();
    module_write(f, RF34DL_FRAME_LEN);
    uint8_t a = wait_ack(need, ACK_WAIT_MS);
    unsigned dt = (unsigned)(now_ms() - t0);

    if (a == need) {
        out("OK %s id=%d band=%s code=%s (0x%02X, %ums)\n", what, id,
            rf34dl_band_name(c->band), hex, need, dt);
    } else {
        out("WARN %s id=%d band=%s code=%s no 0x%02X within %dms\n", what, id,
            rf34dl_band_name(c->band), hex, need, ACK_WAIT_MS);
    }

    s_tx_ok = (a == need);
    s_tx_ms = dt;
    /* 长发成功后模块持续发;STOP/SEND(含失败)都回到 idle。 */
    s_tx_active = (kind == UI_TX_LONG && a == need);
    app_mark_state_dirty();
    link_publishf("{\"t\":\"event\",\"seq\":%lu,\"kind\":\"tx\",\"tx\":\"%s\","
                  "\"id\":%d,\"band\":\"%s\",\"code\":\"%s\",\"ok\":%d,"
                  "\"ms\":%u}",
                  (unsigned long)(link_head() + 1), what, id,
                  rf34dl_band_name(c->band), hex, s_tx_ok ? 1 : 0, dt);

    /* 长发/停止模块本来就不回 F0,不要把"已知行为"画成失败。 */
    if (kind == UI_TX_SEND) {
        ui_note_tx_result(&s_ui, a == RF34DL_ACK_TX_OK, dt);
        ui_mark_dirty();
        ui_refresh(true);
    }
}

static void cmd_erase_locked(const char *args)
{
    store_backup();

    if (arg_eq(args, "all")) {
        codebook_init(&s_codebook);
        if (!store_commit()) {
            return;
        }
        ui_note_count(&s_ui, s_codebook.count);
        ui_note_forget(&s_ui);
        ui_note_idle(&s_ui);
        ui_mark_dirty();
        app_mark_codes_dirty();
        out("OK erased all\n");
        return;
    }

    int id = atoi(args);
    if (id < 1 || id > s_codebook.count) {
        out("ERR id must be 1..%d or 'all'\n", s_codebook.count);
        return;
    }
    codebook_err_t ce = codebook_erase(&s_codebook, id);
    if (ce != CODEBOOK_OK) {
        out("ERR erase: %s\n", codebook_err_name(ce));
        return;
    }
    if (!store_commit()) {
        return;
    }
    ui_note_count(&s_ui, s_codebook.count);
    if (s_codebook.count > 0) {
        ui_note_seed(&s_ui, s_codebook.count, s_codebook.recs[s_codebook.count - 1].band, s_codebook.recs[s_codebook.count - 1].code);
    } else {
        ui_note_forget(&s_ui);
    }
    ui_note_idle(&s_ui);
    ui_mark_dirty();
    app_mark_codes_dirty();
    out("OK erased id=%d (%d left)\n", id, s_codebook.count);
}

static void cmd_erase(const char *args)
{
    if (s_snap_lock) {
        xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    }
    cmd_erase_locked(args);
    if (s_snap_lock) {
        xSemaphoreGive(s_snap_lock);
    }
}

static void cmd_mode(const char *args)
{
    bool cont;
    uint8_t f[RF34DL_FRAME_LEN];
    uint8_t want;

    if (arg_eq(args, "single")) {
        cont = false;
        want = RF34DL_ACK_SINGLE_OK;
    } else if (arg_eq(args, "continuous")) {
        cont = true;
        want = RF34DL_ACK_CONT_OK;
    } else {
        out("ERR usage: MODE single|continuous\n");
        return;
    }

    rf34dl_build_mode(f, cont);
    module_write(f, RF34DL_FRAME_LEN);

    if (wait_ack(want, ACK_WAIT_MS) == want) {
        out("OK mode=%s\n", cont ? "continuous" : "single");
    } else {
        out("WARN mode=%s no 0x%02X within %dms\n", cont ? "continuous" : "single", want,
            ACK_WAIT_MS);
    }
    app_mark_state_dirty();
}

static void cmd_baud(const char *args)
{
    int b = atoi(args);
    int sel = rf34dl_baud_sel(b);
    uint8_t f[RF34DL_FRAME_LEN];

    if (!sel) {
        out("ERR baud must be 4800/9600/57600/115200\n");
        return;
    }
    if (b == s_mod_baud) {
        out("OK baud=%d (unchanged)\n", b);
        return;
    }

    rf34dl_build_baud(f, sel);
    module_write(f, RF34DL_FRAME_LEN);

    /*
     * 实测(2026-11-15,57600):模块改完波特率后,应答可能落在**旧**波特率上,
     * 而且比 80ms 晚 —— 先切本地就会把它读成乱码。做法:先在旧波特率上等满
     * ACK_WAIT_MS;没等到再切过去,按新波特率再等一轮。
     */
    uint8_t a = wait_ack(RF34DL_ACK_BAUD_OK, ACK_WAIT_MS);

    uart_set_baudrate(MODULE_UART, b);
    uart_flush_input(MODULE_UART); /* 丢掉旧波特率上残留的半截字节 */
    s_mod_baud = b;

    if (a != RF34DL_ACK_BAUD_OK) {
        a = wait_ack(RF34DL_ACK_BAUD_OK, ACK_WAIT_MS);
    }

    if (a == RF34DL_ACK_BAUD_OK) {
        out("OK baud=%d\n", b);
    } else {
        /* 没应答 ≠ 没生效:实测中命令确实生效了,只是 F1 没被读到 */
        out("WARN baud=%d: no F1. Module probably did switch; verify with SEND, "
            "or RECOVER to force 9600\n", b);
    }
    app_mark_state_dirty();
}

static bool force_module_9600(void)
{
    uint8_t f[RF34DL_FRAME_LEN];

    rf34dl_build_baud(f, rf34dl_baud_sel(9600));
    uart_flush_input(MODULE_UART);
    module_write(f, RF34DL_FRAME_LEN);
    return wait_ack(RF34DL_ACK_BAUD_OK, ACK_WAIT_MS) == RF34DL_ACK_BAUD_OK;
}

static void cmd_recover(void)
{
    static const int k_bauds[] = {4800, 57600, 115200};
    bool ok = false;

    out("RECOVER: forcing module to 9600 (each try can take up to %dms)\n", 2 * ACK_WAIT_MS);
    s_echo = false;

    /* 先试最常见的 9600:成功时结论才是准确的 */
    uart_set_baudrate(MODULE_UART, 9600);
    if (force_module_9600()) {
        ok = true;
    }

    for (size_t i = 0; !ok && i < sizeof(k_bauds) / sizeof(k_bauds[0]); i++) {
        out("RECOVER: try %d ...\n", k_bauds[i]);
        uart_set_baudrate(MODULE_UART, k_bauds[i]);
        /* 命令按候选波特率发出;若模块真是这个波特率,它会切到 9600 并回 F1 */
        ok = force_module_9600();

        if (!ok) {
            /* 应答也可能落在切换后的 9600 上:不再重复发命令,只按 9600 听一轮 */
            uart_set_baudrate(MODULE_UART, 9600);
            ok = (wait_ack(RF34DL_ACK_BAUD_OK, ACK_WAIT_MS) == RF34DL_ACK_BAUD_OK);
        }
    }

    uart_set_baudrate(MODULE_UART, 9600);
    s_mod_baud = 9600;
    s_echo = true;

    if (ok) {
        out("OK module baud=9600\n");
    } else {
        out("ERR RECOVER failed: check 3V3/GND/TXD-RXD and module power\n");
    }
    app_mark_state_dirty();
}

static void cmd_raw(const char *args)
{
    uint8_t buf[32];
    int n = parse_hex(args, buf, sizeof(buf));

    if (n < 0) {
        out("ERR odd number of hex digits\n");
        return;
    }
    if (n == 0) {
        out("ERR usage: RAW FD F3 03 01 02 03 60 DF\n");
        return;
    }
    module_write(buf, (size_t)n);
    out("OK raw %d B\n", n);
}

static void cmd_disp(const char *args)
{
    const oled_stats_t *st = oled_stats();
    app_mark_state_dirty();

    if (!*args) {
        out("OK disp=%s present=%s test=%s flush=%lu err=%lu\n",
            s_ui.enabled ? "on" : "off", s_oled_ok ? "yes" : "no",
            s_test_until_ms ? "on" : "off", (unsigned long)st->flush_count,
            (unsigned long)st->flush_err);
        return;
    }

    if (arg_eq(args, "on")) {
        if (!s_oled_ok) {
            out("ERR oled not present at 0x%02X\n", OLED_PANEL_ADDR);
            return;
        }
        esp_err_t e = oled_set_on(true);
        if (e != ESP_OK) {
            out("ERR oled on: %s\n", esp_err_to_name(e));
            return;
        }
        s_ui.enabled = true;
        s_test_until_ms = 0;
        s_test_drawn = false;
        ui_mark_dirty();
        ui_refresh(true);
        out("OK disp=on\n");
        return;
    }

    if (arg_eq(args, "off")) {
        if (s_oled_ok) {
            esp_err_t e = oled_set_on(false);
            if (e != ESP_OK) {
                out("ERR oled off: %s\n", esp_err_to_name(e));
                return;
            }
        }
        s_ui.enabled = false;
        s_test_until_ms = 0;
        s_test_drawn = false;
        out("OK disp=off\n");
        return;
    }

    if (arg_prefix(args, "test") && (args[4] == 0 || isspace((unsigned char)args[4]))) {
        if (!s_oled_ok) {
            out("ERR oled not present at 0x%02X\n", OLED_PANEL_ADDR);
            return;
        }
        int secs = atoi(args + 4);
        if (secs <= 0) {
            secs = 10;
        }
        if (secs > 300) {
            secs = 300;
        }
        if (!s_ui.enabled) {
            esp_err_t e = oled_set_on(true);
            if (e != ESP_OK) {
                out("ERR oled on: %s\n", esp_err_to_name(e));
                return;
            }
            s_ui.enabled = true;
        }
        s_boot_until_ms = 0;
        s_test_until_ms = now_ms() + (int64_t)secs * 1000;
        s_test_drawn = false;
        ui_mark_dirty();
        ui_refresh(true);
        out("OK disp=test %ds (DISP ON to leave test)\n", secs);
        return;
    }

    if (arg_eq(args, "show")) {
        if (s_test_until_ms && now_ms() < s_test_until_ms) {
            ui_test_pattern(&s_gfx);
        } else {
            s_test_until_ms = 0;
            s_test_drawn = false;
            ui_draw_model();
        }
        char line[GFX_W + 1];
        out("# fb %dx%d '#'=on (screen content, not read back from glass)\n", GFX_W, GFX_H);
        for (int y = 0; y < GFX_H; y++) {
            gfx_ascii_line(&s_gfx, y, line);
            out("%s\n", line);
        }
        out("OK fb %dx%d\n", GFX_W, GFX_H);
        return;
    }

    out("ERR usage: DISP ON|OFF|TEST [s]|SHOW\n");
}

/* ---------------- 网络 / BLE / 快照命令 ---------------- */

static void cmd_wifi(const char *args)
{
    net_info_t ni;

    if (!*args || arg_eq(args, "status")) {
        net_get_info(&ni);
        out("OK wifi mode=%s ap_on=%d has_creds=%d ap=%s ap_ip=%s clients=%d "
            "sta=%s sta_ip=%s rssi=%d\n",
            ni.mode, ni.ap_on ? 1 : 0, ni.has_creds ? 1 : 0,
            ni.ap_ssid, ni.ap_ip, ni.ap_clients,
            ni.sta_connected ? ni.sta_ssid : "-", ni.sta_ip, ni.rssi);
        return;
    }

    if (arg_eq(args, "clear") || arg_eq(args, "forget")) {
        net_clear_sta();
        out("OK wifi cleared; AP mode\n");
        app_mark_state_dirty();
        return;
    }

    char ssid[33];
    char pass[65];
    const char *sp = strchr(args, ' ');

    if (sp) {
        size_t n = (size_t)(sp - args);
        if (n >= sizeof(ssid)) {
            out("ERR ssid too long\n");
            return;
        }
        memcpy(ssid, args, n);
        ssid[n] = 0;
        snprintf(pass, sizeof(pass), "%s", sp + 1);
    } else {
        snprintf(ssid, sizeof(ssid), "%s", args);
        pass[0] = 0;
    }

    esp_err_t e = net_set_sta(ssid, pass);
    if (e == ESP_OK) {
        out("OK wifi ssid=%s connecting (AP stays up)\n", ssid);
    } else {
        out("ERR wifi: %s\n", esp_err_to_name(e));
    }
    app_mark_state_dirty();
}

static void cmd_ble(const char *args)
{
    if (arg_eq(args, "on")) {
        ble_link_set_enabled(true);
        out("OK ble=on\n");
    } else if (arg_eq(args, "off")) {
        ble_link_set_enabled(false);
        out("OK ble=off (advertising stopped, current link dropped)\n");
    } else {
        out("OK ble=%s adv=%s conn=%s name=%s\n", ble_link_is_on() ? "on" : "off",
            ble_link_advertising() ? "yes" : "no",
            ble_link_connected() ? "yes" : "no", s_dev_name);
    }
    app_mark_state_dirty();
}

static void cmd_pin(app_src_t src)
{
    if (src != APP_SRC_SERIAL) {
        /* 远程通道能读 PIN 就等于没有 PIN。只允许物理串口。 */
        out("ERR PIN is serial-only\n");
        return;
    }
    out("OK ble_pin=%s (enter in Android app; shown on OLED for 5s at boot)\n", s_ble_pin);
}

static void cmd_ota(const char *args, app_src_t src)
{
    if (src != APP_SRC_SERIAL) {
        /* An HTTP/BLE caller must not be able to arm the flasher. */
        out("ERR OTA is serial-only\n");
        return;
    }
    if (arg_eq(args, "off")) {
        ota_disarm();
        out("OK ota=off\n");
        ui_note_idle(&s_ui);
        ui_mark_dirty();
        app_mark_state_dirty();
        return;
    }
    if (*args != 0 && !arg_eq(args, "on")) {
        out("ERR usage: OTA|OTA OFF\n");
        return;
    }

    char token[16];
    if (!ota_arm(token, sizeof(token))) {
        out("ERR ota arm failed\n");
        return;
    }
    out("OK ota armed token=%s ttl=%us\n", token, (unsigned)ota_remaining_s());
    out("# upload: POST /api/ota  X-RFLAB-OTA: <token>  body=firmware.bin\n");

    /* token 8 hex + "OTA " + NUL = 13;按 token 缓冲留足,OLED 侧再截 12 列。 */
    char line[sizeof(token) + 5];
    snprintf(line, sizeof(line), "OTA %s", token);
    ui_note_boot(&s_ui, line, "10 MIN");
    s_boot_until_ms = now_ms() + 10000;
    ui_mark_dirty();
    app_mark_state_dirty();
}

static void cmd_state(void)
{
    static char buf[APP_STATE_JSON_MAX];
    app_copy_state(buf, sizeof(buf));
    /* 状态 JSON 比普通日志长:直接写控制台,避免被 out() 的 1KB 缓冲截断。 */
    con_write(buf, (int)strlen(buf));
    con_write("\n", 1);
}

/* 自测:从设备内部访问自己的 HTTP/SSE,不依赖外部 Wi-Fi 客户端。
 * 这不是替代真机浏览器验收,只是把"服务起来了"与"路由/响应可用"分开。 */
static void cmd_webtest(void)
{
    char base[32];
    char urls[3][64];
    snprintf(base, sizeof(base), "http://%s", net_ip());
    snprintf(urls[0], sizeof(urls[0]), "%s/api/hello", base);
    snprintf(urls[1], sizeof(urls[1]), "%s/api/state", base);
    snprintf(urls[2], sizeof(urls[2]), "%s/api/events", base);

    for (size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
        esp_http_client_config_t cfg = {
            .url = urls[i],
            .timeout_ms = 4000,
            .disable_auto_redirect = true,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) {
            out("ERR webtest init\n");
            return;
        }
        esp_err_t e = esp_http_client_open(c, 0);
        if (e != ESP_OK) {
            out("ERR webtest open %s: %s\n", urls[i], esp_err_to_name(e));
            esp_http_client_cleanup(c);
            continue;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        char body[220];
        int n = esp_http_client_read(c, body, sizeof(body) - 1);
        if (n < 0) {
            n = 0;
        }
        body[n] = 0;
        for (int k = 0; k < n; k++) {
            if (body[k] == '\n' || body[k] == '\r') {
                body[k] = ' ';
            }
        }
        out("OK webtest %d %s -> %s\n", status, urls[i], body);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }

    /* POST 走完整入队路径:命令输出会同时打到串口,便于确认 main 真的执行了。 */
    char post_url[64];
    snprintf(post_url, sizeof(post_url), "%s/api/cmd", base);
    esp_http_client_config_t pcfg = {
        .url = post_url,
        .timeout_ms = 4000,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t pc = esp_http_client_init(&pcfg);
    if (pc) {
        const char *json = "{\"cmd\":\"DISP TEST 1\"}";
        esp_http_client_set_method(pc, HTTP_METHOD_POST);
        esp_http_client_set_header(pc, "Content-Type", "application/json");
        esp_http_client_set_post_field(pc, json, (int)strlen(json));
        esp_err_t pe = esp_http_client_perform(pc);
        int pstatus = esp_http_client_get_status_code(pc);
        if (pe == ESP_OK) {
            out("OK webtest POST %d /api/cmd -> DISP TEST 1\n", pstatus);
        } else {
            out("ERR webtest POST: %s\n", esp_err_to_name(pe));
        }
        esp_http_client_cleanup(pc);
    }
}

/* ---------------- 命令分发 ---------------- */

static char s_line[160];
static int s_linelen = 0;

static void handle_line(char *line, app_src_t src)
{
    char *p = line;
    char cmd[16];
    int i = 0;

    while (*p == ' ') {
        p++;
    }
    if (*p == 0) {
        return;
    }

    while (*p && !isspace((unsigned char)*p) && i < (int)sizeof(cmd) - 1) {
        cmd[i++] = (char)toupper((unsigned char)*p++);
    }
    cmd[i] = 0;
    while (*p == ' ') {
        p++;
    }
    /* 去掉尾部空白,免得 "MODE single " 被判成未知参数 */
    size_t plen = strlen(p);
    while (plen > 0 && (p[plen - 1] == ' ' || p[plen - 1] == '\t' || p[plen - 1] == '\r')) {
        p[--plen] = 0;
    }

    if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "INFO") == 0) {
        cmd_info();
    } else if (strcmp(cmd, "LIST") == 0) {
        if (arg_eq(p, "long")) {
            cmd_list_long();
        } else {
            cmd_list();
        }
    } else if (strcmp(cmd, "NEW") == 0) {
        cmd_new(p);
    } else if (strcmp(cmd, "NAME") == 0) {
        cmd_set_text(p, 0);
    } else if (strcmp(cmd, "SYM") == 0) {
        cmd_set_text(p, 1);
    } else if (strcmp(cmd, "GROUP") == 0) {
        cmd_set_text(p, 2);
    } else if (strcmp(cmd, "EDIT") == 0) {
        cmd_edit(p);
    } else if (strcmp(cmd, "SEND") == 0) {
        do_tx(p, RF34DL_DUR_NORMAL, "send");
    } else if (strcmp(cmd, "LONG") == 0) {
        do_tx(p, RF34DL_DUR_LONG, "long");
    } else if (strcmp(cmd, "STOP") == 0) {
        do_tx(p, RF34DL_DUR_STOP, "stop");
    } else if (strcmp(cmd, "DEDUP") == 0) {
        cmd_dedup();
    } else if (strcmp(cmd, "ERASE") == 0) {
        cmd_erase(p);
    } else if (strcmp(cmd, "MODE") == 0) {
        cmd_mode(p);
    } else if (strcmp(cmd, "BAUD") == 0) {
        cmd_baud(p);
    } else if (strcmp(cmd, "RECOVER") == 0) {
        cmd_recover();
    } else if (strcmp(cmd, "RAW") == 0) {
        cmd_raw(p);
    } else if (strcmp(cmd, "MON") == 0) {
        if (arg_eq(p, "on")) {
            s_mon = true;
            out("OK mon=on\n");
        } else if (arg_eq(p, "off")) {
            s_mon = false;
            out("OK mon=off\n");
        } else {
            out("ERR usage: MON ON|OFF\n");
        }
    } else if (strcmp(cmd, "DISP") == 0) {
        cmd_disp(p);
    } else if (strcmp(cmd, "WIFI") == 0) {
        cmd_wifi(p);
    } else if (strcmp(cmd, "BLE") == 0) {
        cmd_ble(p);
    } else if (strcmp(cmd, "PIN") == 0) {
        cmd_pin(src);
    } else if (strcmp(cmd, "OTA") == 0) {
        cmd_ota(p, src);
    } else if (strcmp(cmd, "STATE") == 0) {
        cmd_state();
    } else if (strcmp(cmd, "WEBTEST") == 0) {
        cmd_webtest();
    } else if (strcmp(cmd, "ECHO") == 0) {
        if (arg_eq(p, "on")) {
            s_echo = true;
            out("OK echo=on\n");
        } else if (arg_eq(p, "off")) {
            s_echo = false;
            out("OK echo=off\n");
        } else {
            out("ERR usage: ECHO ON|OFF\n");
        }
    } else if (strcmp(cmd, "RESET") == 0) {
        out("OK reset\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    } else {
        out("ERR unknown '%s' (HELP)\n", cmd);
    }
}

static void feed_console(const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        char c = (char)b[i];

        if (c == '\r' || c == '\n') {
            if (s_linelen > 0) {
                s_line[s_linelen] = 0;
                if (app_submit_cmd(s_line, APP_SRC_SERIAL) != 0) {
                    out("ERR busy: command queue full, try again\n");
                }
                s_linelen = 0;
            }
        } else if (c >= 32 && c < 127) {
            if (s_linelen < (int)sizeof(s_line) - 1) {
                s_line[s_linelen++] = c;
            }
        }
    }
}

static int console_read(uint8_t *buf, int max)
{
    int n = 0;

    if (s_uart0) {
        int r = uart_read_bytes(UART_NUM_0, buf, (uint32_t)max, 0);
        if (r > 0) {
            n = r;
        }
    }
    if (s_usb && n < max) {
        int r = usb_serial_jtag_read_bytes(buf + n, (uint32_t)(max - n), 0);
        if (r > 0) {
            n += r;
        }
    }
    return n;
}

/* ---------------- 命令执行 / 启动 ---------------- */

static void execute_one_queued(void)
{
    cmd_item_t it;

    if (!s_cmd_q || xQueueReceive(s_cmd_q, &it, 0) != pdPASS) {
        return;
    }

    s_capture = true;
    s_capture_len = 0;
    s_capture_buf[0] = 0;

    handle_line(it.line, (app_src_t)it.src);

    s_capture = false;
    if (s_capture_len > 0) {
        char esc[LINK_LINE_MAX - 96];
        link_json_escape(s_capture_buf, esc, sizeof(esc));
        link_publishf("{\"t\":\"log\",\"seq\":%lu,\"line\":\"%s\"}",
                      (unsigned long)(link_head() + 1), esc);
    }
    s_capture_len = 0;
    app_mark_state_dirty();
}

static void load_ble_pin(void)
{
    nvs_handle_t h;
    uint32_t pin = 0;
    bool ok = false;

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_get_u32(h, "ble_pin", &pin) == ESP_OK && pin <= 9999) {
            ok = true;
        } else {
            pin = 1000 + (esp_random() % 9000);
            nvs_set_u32(h, "ble_pin", pin);
            nvs_commit(h);
            ok = true;
        }
        nvs_close(h);
    }
    if (!ok) {
        pin = 1234;
    }
    snprintf(s_ble_pin, sizeof(s_ble_pin), "%04lu", (unsigned long)pin);
}

static void make_device_name(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_dev_name, sizeof(s_dev_name), "RF-Lab-%02X%02X", mac[4], mac[5]);
}

/* ---------------- main ---------------- */

/* OTA progress/reboot pump. Runs on the main loop (single producer for
 * state/event publishing); the HTTP handler only streams bytes into ota.c. */
static void poll_ota(void)
{
    static char last_phase[8] = "";
    static int last_pct = -1;

    if (ota_pending_verify() && now_ms() > 6000) {
        if (ota_confirm_boot()) {
            out("# ota new image confirmed (rollback window closed)\n");
        }
    }

    const char *ph = ota_phase();
    if (strcmp(ph, last_phase) != 0) {
        snprintf(last_phase, sizeof(last_phase), "%s", ph);
        if (strcmp(ph, "error") == 0) {
            out("# ota error: %s\n", ota_error());
        } else if (strcmp(ph, "done") == 0) {
            out("# ota image written; rebooting\n");
        } else if (strcmp(ph, "armed") == 0) {
            out("# ota armed (%us)\n", (unsigned)ota_remaining_s());
        }
        app_mark_state_dirty();
    }

    int pct = ota_progress();
    if (pct != last_pct) {
        last_pct = pct;
        app_mark_state_dirty();
    }

    if (ota_reboot_due()) {
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
    }
}

void app_main(void)
{
    init_console();
    init_module();

    esp_err_t oe = oled_init(PIN_OLED_SDA, PIN_OLED_SCL, OLED_I2C_HZ);
    s_oled_ok = (oe == ESP_OK);

    out("\n# RF-Lab fw=%s (ESP32-C3 + YS-RF34DL)\n", RFLAB_FW_VERSION);
    out("# module UART1 %d 8N1 TX=GPIO%d RX=GPIO%d\n", s_mod_baud, PIN_MODULE_TX, PIN_MODULE_RX);
    out("# console uart0:%s usb:%s %d 8N1\n", s_uart0 ? "on" : "off", s_usb ? "on" : "off",
        CONSOLE_BAUD);
    out("# oled 72x40 @0x%02X i2c%d sda=GPIO%d scl=GPIO%d: %s\n", OLED_PANEL_ADDR,
        oled_bus_hz(), PIN_OLED_SDA, PIN_OLED_SCL,
        s_oled_ok ? "ok" : esp_err_to_name(oe));
    if (!s_oled_ok) {
        /* 屏不在不是错误:0.4.0 的纯串口行为必须原样保留。 */
        out("# oled unavailable (%s); continuing with serial console only\n",
            esp_err_to_name(oe));
    }

    link_init();
    s_cmd_q = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(cmd_item_t));
    s_snap_lock = xSemaphoreCreateMutex();

    bool nvs_ok = init_store();
    load_ble_pin();
    make_device_name();
    ota_init();
    out("# stored %d code(s)%s\n", s_codebook.count, nvs_ok ? "" : " (NVS unavailable)");
    out("# build=%s ui=%s ota_slot=%d pending_verify=%d\n",
        RFLAB_BUILD_ID, RFLAB_UI_HASH, ota_running_slot(), ota_pending_verify() ? 1 : 0);

    esp_err_t ne = net_init();
    if (ne != ESP_OK) {
        out("# net init failed: %s (Web UI unavailable)\n", esp_err_to_name(ne));
    }
    esp_err_t be = ble_link_start(s_dev_name, s_ble_pin);
    if (be != ESP_OK) {
        out("# ble init failed: %s (BLE unavailable)\n", esp_err_to_name(be));
    }
    esp_err_t we = web_server_start();
    if (we != ESP_OK) {
        out("# web init failed: %s\n", esp_err_to_name(we));
    }

    if (s_snap_lock) {
        xTaskCreate(snapshot_task, "snap", 4096, NULL, 3, NULL);
    }

    char title[UI_COLS + 1];
    snprintf(title, sizeof(title), "RF-Lab %s", RFLAB_FW_VERSION);
    ui_init(&s_ui, title, s_codebook.count, MAX_CODES, s_oled_ok);
    ui_note_counters(&s_ui, (unsigned)s_frames, (unsigned)s_acks, (unsigned)s_parser.stray);
    if (s_codebook.count > 0) {
        ui_note_seed(&s_ui, s_codebook.count, s_codebook.recs[s_codebook.count - 1].band, s_codebook.recs[s_codebook.count - 1].code);
    }
    if (!nvs_ok) {
        ui_note_error(&s_ui, "NVS FAIL");
    } else if (s_oled_ok) {
        char pinline[UI_COLS + 1];
        char netline[UI_COLS + 1];
        net_info_t ni;
        net_get_info(&ni);
        snprintf(pinline, sizeof(pinline), "BLE PIN %s", s_ble_pin);
        snprintf(netline, sizeof(netline), "%s",
                 strcmp(ni.mode, "connecting") == 0 ? "WIFI..." : net_ip());
        ui_note_boot(&s_ui, pinline, netline);
        s_boot_until_ms = now_ms() + 5000;
    }
    ui_mark_dirty();
    ui_refresh(true);

    net_info_t boot_net;
    net_get_info(&boot_net);
    out("# net mode=%s ap=%s pass=%s ap_ip=%s sta=%s sta_ip=%s\n",
        boot_net.mode, boot_net.ap_ssid, RFLAB_AP_PASS, boot_net.ap_ip,
        boot_net.sta_connected ? boot_net.sta_ssid : "-", boot_net.sta_ip);
    out("# ble name=%s pin=%s (serial PIN; also OLED 5s)\n", s_dev_name, s_ble_pin);
    if (strcmp(boot_net.mode, "connecting") == 0) {
        out("# web: STA connecting; AP fallback opens after 15s if it fails\n");
    } else {
        out("# web http://%s/  api /api/state /api/events\n", net_ip());
    }
    out("# press a remote button to learn; HELP for commands\n");

    uint8_t tmp[256];

    for (;;) {
        int n = console_read(tmp, sizeof(tmp));
        if (n > 0) {
            feed_console(tmp, n);
        }
        poll_net_note();
        poll_ota();
        execute_one_queued();
        pump(5);
        ui_refresh(false);
    }
}
