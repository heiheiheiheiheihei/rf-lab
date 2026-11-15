/*
 * RF-Lab 0.4.0 — ESP32-C3 OLED + YS-RF34DL(315/433 编解码模块)
 *
 * 分工:
 *   · 解码、编码、发射全部在模块里完成,ESP32 只做「串口 ↔ 文本命令行」;
 *   · 模块接 UART1(9600 8N1,TX=GPIO3 → 模块 RXD,RX=GPIO4 ← 模块 TXD);
 *   · 命令行走 原生 USB-Serial-JTAG + UART0(GPIO20/21),115200 8N1;
 *   · 学码 = 按一下原遥控器(模块主动输出 8 字节帧),固件自动入库并打印;
 *   · 码存 NVS,掉电不丢;同码 600ms 内只算一次(模块默认连续输出)。
 *
 * 刻意不做:
 *   · 启动时不向模块发任何配置命令 —— 模块行为未公开,少发一条就少一个假证据源;
 *   · 不解析 EV1527 位结构 —— 模块给的 4 字节原样存、原样回放,不做推断。
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
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "rf34dl.h"

#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.4.0"
#endif

/* ---------------- 引脚与参数 ---------------- */

#define CONSOLE_BAUD 115200
#define PIN_CONSOLE_TX 21 /* UART0,板载 USB-TTL 时可用 */
#define PIN_CONSOLE_RX 20

#define MODULE_UART UART_NUM_1
#define PIN_MODULE_TX 3 /* ESP32 发 → 模块 RXD */
#define PIN_MODULE_RX 4 /* 模块 TXD → ESP32 收 */

#define MODULE_BAUD_DEFAULT 9600
#define MAX_CODES 32
#define DEDUP_MS 600
#define ACK_WAIT_MS 500

#define NVS_NS "rf34dl"
#define NVS_KEY "codes"
#define STORE_MAGIC0 'R'
#define STORE_MAGIC1 '3'
#define STORE_VER 1

typedef struct {
    uint8_t band;
    uint8_t code[RF34DL_CODE_LEN];
} saved_t;

typedef struct {
    uint8_t magic[2];
    uint8_t ver;
    uint8_t count;
    saved_t items[MAX_CODES];
} store_t;

/* ---------------- 状态 ---------------- */

static bool s_uart0 = false;
static bool s_usb = false;
static bool s_echo = true;  /* 打印解码事件 */
static bool s_mon = false;  /* 打印模块原始字节 */
static int s_mod_baud = MODULE_BAUD_DEFAULT;

static saved_t s_saved[MAX_CODES];
static int s_count = 0;

static rf34dl_parser_t s_parser;

static uint32_t s_rx_bytes = 0;
static uint32_t s_frames = 0;
static uint32_t s_acks = 0;
static uint32_t s_dups = 0;
static uint8_t s_ack_last = 0;

static uint8_t s_last_band = 0;
static uint8_t s_last_code[RF34DL_CODE_LEN];
static int64_t s_last_ms = 0;

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
    char buf[320];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        con_write(buf, n);
    }
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

static bool store_load(void)
{
    nvs_handle_t h;
    store_t st;
    size_t len = sizeof(st);

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e = nvs_get_blob(h, NVS_KEY, &st, &len);
    nvs_close(h);

    if (e != ESP_OK || len != sizeof(st)) {
        return false;
    }
    if (st.magic[0] != STORE_MAGIC0 || st.magic[1] != STORE_MAGIC1 ||
        st.ver != STORE_VER || st.count > MAX_CODES) {
        return false;
    }

    memcpy(s_saved, st.items, sizeof(saved_t) * st.count);
    s_count = st.count;
    return true;
}

static void store_save(void)
{
    store_t st;
    nvs_handle_t h;

    memset(&st, 0, sizeof(st));
    st.magic[0] = STORE_MAGIC0;
    st.magic[1] = STORE_MAGIC1;
    st.ver = STORE_VER;
    st.count = (uint8_t)s_count;
    memcpy(st.items, s_saved, sizeof(saved_t) * (size_t)s_count);

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        out("ERR nvs open failed\n");
        return;
    }
    if (nvs_set_blob(h, NVS_KEY, &st, sizeof(st)) == ESP_OK) {
        nvs_commit(h);
    } else {
        out("ERR nvs write failed\n");
    }
    nvs_close(h);
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

static int find_code(uint8_t band, const uint8_t *code)
{
    for (int i = 0; i < s_count; i++) {
        if (s_saved[i].band == band &&
            memcmp(s_saved[i].code, code, RF34DL_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
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

static void handle_frame(const rf34dl_frame_t *f)
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
    s_last_ms = now;
    s_last_band = f->band;
    memcpy(s_last_code, f->code, RF34DL_CODE_LEN);

    int idx = find_code(f->band, f->code);
    if (idx >= 0) {
        if (s_echo) {
            out("RX id=%d band=%s code=%s\n", idx + 1, rf34dl_band_name(f->band), hex);
        }
        return;
    }

    if (s_count >= MAX_CODES) {
        if (s_echo) {
            out("RX band=%s code=%s (store full, not saved)\n", rf34dl_band_name(f->band), hex);
        }
        return;
    }

    s_saved[s_count].band = f->band;
    memcpy(s_saved[s_count].code, f->code, RF34DL_CODE_LEN);
    s_count++;
    store_save();

    if (s_echo) {
        out("LEARN id=%d band=%s code=%s\n", s_count, rf34dl_band_name(f->band), hex);
    }
}

static void handle_ack(uint8_t a)
{
    const char *name = "unknown";

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
    default:
        break;
    }
    if (s_echo) {
        out("ACK 0x%02X %s\n", a, name);
    }
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

static void cmd_help(void)
{
    out("commands (115200 8N1):\n");
    out("  HELP                  this list\n");
    out("  INFO                  fw / module baud / counters\n");
    out("  LIST                  stored codes: id band code\n");
    out("  SEND <id>             transmit once (frame dur=03)\n");
    out("  LONG <id>             start continuous transmit (dur=FE)\n");
    out("  STOP <id>             stop continuous transmit (dur=FF)\n");
    out("  ERASE <id|all>        delete one code / all codes\n");
    out("  MODE single|continuous  module decode output mode\n");
    out("  BAUD <n>              module baud 4800/9600/57600/115200\n");
    out("  RECOVER               scan module baud and force it back to 9600\n");
    out("  RAW <hex>             send raw bytes to module (debug)\n");
    out("  MON ON|OFF            hex-dump every module byte (debug)\n");
    out("  ECHO ON|OFF           print decoded frames (default ON)\n");
    out("  RESET                 restart ESP32\n");
    out("\nlearn: press a remote button; firmware prints LEARN id=... automatically\n");
}

static void cmd_info(void)
{
    out("# RF-Lab fw=%s (ESP32-C3 + YS-RF34DL)\n", RFLAB_FW_VERSION);
    out("# module UART1 %d 8N1 TX=GPIO%d RX=GPIO%d\n", s_mod_baud, PIN_MODULE_TX, PIN_MODULE_RX);
    out("# console uart0:%s usb:%s %d 8N1\n", s_uart0 ? "on" : "off", s_usb ? "on" : "off",
        CONSOLE_BAUD);
    out("OK stored=%d/%d rx_bytes=%lu frames=%lu acks=%lu dups=%lu stray=%lu resync=%lu\n",
        s_count, MAX_CODES, (unsigned long)s_rx_bytes, (unsigned long)s_frames,
        (unsigned long)s_acks, (unsigned long)s_dups, (unsigned long)s_parser.stray,
        (unsigned long)s_parser.dropped);
}

static void cmd_list(void)
{
    if (s_count == 0) {
        out("(empty; press a remote button and watch for LEARN)\n");
        return;
    }
    for (int i = 0; i < s_count; i++) {
        char hex[9];
        hex4(s_saved[i].code, hex);
        out("%2d  %-4s %s\n", i + 1, rf34dl_band_name(s_saved[i].band), hex);
    }
}

static void do_tx(const char *args, uint8_t dur, const char *what)
{
    int id = atoi(args);
    uint8_t f[RF34DL_FRAME_LEN];
    char hex[9];

    if (id < 1 || id > s_count) {
        out("ERR id must be 1..%d (LIST)\n", s_count);
        return;
    }

    saved_t *c = &s_saved[id - 1];
    hex4(c->code, hex);
    if (rf34dl_build_tx(f, c->band, dur, c->code) != RF34DL_FRAME_LEN) {
        out("ERR build failed\n");
        return;
    }

    module_write(f, RF34DL_FRAME_LEN);
    uint8_t a = wait_ack(RF34DL_ACK_TX_OK, ACK_WAIT_MS);

    if (a == RF34DL_ACK_TX_OK) {
        out("OK %s id=%d band=%s code=%s\n", what, id, rf34dl_band_name(c->band), hex);
    } else {
        out("WARN %s id=%d band=%s code=%s no F0 within %dms\n", what, id,
            rf34dl_band_name(c->band), hex, ACK_WAIT_MS);
    }
}

static void cmd_erase(const char *args)
{
    if (arg_eq(args, "all")) {
        s_count = 0;
        store_save();
        out("OK erased all\n");
        return;
    }

    int id = atoi(args);
    if (id < 1 || id > s_count) {
        out("ERR id must be 1..%d or 'all'\n", s_count);
        return;
    }
    for (int i = id - 1; i < s_count - 1; i++) {
        s_saved[i] = s_saved[i + 1];
    }
    s_count--;
    store_save();
    out("OK erased id=%d (%d left)\n", id, s_count);
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
     * 应答可能落在旧波特率上(先回再切),也可能落在新波特率上(先切再回)。
     * 两段都收:先在旧波特率听 80ms,切过去再听 400ms;任一段收到 F1 就算成功。
     */
    uint8_t a = wait_ack(RF34DL_ACK_BAUD_OK, 80);

    vTaskDelay(pdMS_TO_TICKS(20));
    uart_set_baudrate(MODULE_UART, b);
    uart_flush_input(MODULE_UART);
    s_mod_baud = b;

    if (a != RF34DL_ACK_BAUD_OK) {
        a = wait_ack(RF34DL_ACK_BAUD_OK, 400);
    }

    if (a == RF34DL_ACK_BAUD_OK) {
        out("OK baud=%d\n", b);
    } else {
        out("WARN baud=%d: no F1. Module may already be at %d; if not, run RECOVER\n", b, b);
    }
}

static void cmd_recover(void)
{
    static const int k_bauds[] = {4800, 9600, 57600, 115200};
    uint8_t force9600[RF34DL_FRAME_LEN];

    rf34dl_build_baud(force9600, rf34dl_baud_sel(9600));
    out("RECOVER: probing module baud, forcing 9600\n");
    s_echo = false;

    for (size_t i = 0; i < sizeof(k_bauds) / sizeof(k_bauds[0]); i++) {
        int b = k_bauds[i];

        uart_set_baudrate(MODULE_UART, b);
        uart_flush_input(MODULE_UART);
        module_write(force9600, RF34DL_FRAME_LEN);
        vTaskDelay(pdMS_TO_TICKS(80));

        uart_set_baudrate(MODULE_UART, 9600);
        uart_flush_input(MODULE_UART);
        module_write(force9600, RF34DL_FRAME_LEN);

        uint8_t a = wait_ack(RF34DL_ACK_BAUD_OK, 250);
        out("RECOVER try %6d: %s\n", b, a == RF34DL_ACK_BAUD_OK ? "F1" : "no ack");
        if (a == RF34DL_ACK_BAUD_OK) {
            s_echo = true;
            s_mod_baud = 9600;
            out("OK module baud=9600\n");
            return;
        }
    }

    s_echo = true;
    s_mod_baud = 9600; /* 本地按默认值继续,至少命令行还能用 */
    uart_set_baudrate(MODULE_UART, 9600);
    out("ERR RECOVER failed: check 3V3/GND/TXD-RXD and module power\n");
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

/* ---------------- 命令分发 ---------------- */

static char s_line[160];
static int s_linelen = 0;

static void handle_line(char *line)
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
        cmd_list();
    } else if (strcmp(cmd, "SEND") == 0) {
        do_tx(p, RF34DL_DUR_NORMAL, "tx");
    } else if (strcmp(cmd, "LONG") == 0) {
        do_tx(p, RF34DL_DUR_LONG, "long");
    } else if (strcmp(cmd, "STOP") == 0) {
        do_tx(p, RF34DL_DUR_STOP, "stop");
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
                handle_line(s_line);
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

/* ---------------- main ---------------- */

void app_main(void)
{
    init_console();
    init_module();

    out("\n# RF-Lab fw=%s (ESP32-C3 + YS-RF34DL)\n", RFLAB_FW_VERSION);
    out("# module UART1 %d 8N1 TX=GPIO%d RX=GPIO%d\n", s_mod_baud, PIN_MODULE_TX, PIN_MODULE_RX);
    out("# console uart0:%s usb:%s %d 8N1\n", s_uart0 ? "on" : "off", s_usb ? "on" : "off",
        CONSOLE_BAUD);

    bool nvs_ok = init_store();
    out("# stored %d code(s)%s\n", s_count, nvs_ok ? "" : " (NVS unavailable)");
    out("# press a remote button to learn; HELP for commands\n");

    uint8_t tmp[256];

    for (;;) {
        int n = console_read(tmp, sizeof(tmp));
        if (n > 0) {
            feed_console(tmp, n);
        }
        pump(5);
    }
}
