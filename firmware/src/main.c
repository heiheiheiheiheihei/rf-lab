/*
 * main.c — RF-Lab 固件入口(本批:USB-only,阶段 1 骨架)
 *
 * 本文件只做四件事:
 *   1. 上电即把 RF_KILL 拉低(**硬件发射门控,低有效** = 默认禁止发射)
 *   2. 初始化板载 WS2812 并按硬件文档 §2.7 的指示语显示
 *   3. 在 **UART0(CH343P)** 上跑契约 §2 的帧协议
 *   4. 实现 §3.1 的四条消息:HELLO / HELLO_ACK / GET_CAPS / CAPS / PING / PONG / ERROR
 *
 * 🔴 2026-10-21 载体变更(实测驱动):协议**不再**跑在原生 USB Serial/JTAG 上。
 *   本板的原生 USB 从未枚举成功(2026-10-18:435 次 connect / 0 次成功;
 *   2026-10-21 换口复测 150s 零事件),唯一可用通路是 CH343P → UART0。
 *   因此 UART0 由协议独占、控制台与日志关闭(见 sdkconfig.defaults 注释)。
 *   帧层(frame.c)与载体无关,本次**未改动一行**。
 *
 * 明确**没做**(写出来是为了避免"看起来做了"):
 *   - 射频模块驱动与探测 ⇒ CAPS.modules[].present 一律 false(**未探测**,不是"确认没有")
 *   - 扫频 / 捕获 / 发射 / 门禁 / 令牌校验(阶段 2–4)
 *   - `RF_KILL` 的软件切换:契约里**还没有这个字段**(已知归属缺口,
 *     见接线文档 §6)。⚠️ 不要为此自创一个消息类型 —— 那会让 App 侧错位。
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_rom_uart.h"
#include "hal/uart_ll.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* 控制通道:CH343P ↔ UART0(GPIO43=TX / GPIO44=RX)。
 * ⚠️ 921600 不是随手选的:本板这条通路已在 921600 上跑过连续 I/Q 传输
 *    (esp-sdr 实测),是唯一被验证过的可用速率。 */
#define RFLAB_UART_PORT    UART_NUM_0
#ifndef RFLAB_UART_BAUD
#define RFLAB_UART_BAUD    921600
#endif
#define PIN_UART0_TX       43
#define PIN_UART0_RX       44

#include "frame.h"
#include "led.h"
#include "pairing.h"
#include "rf_probe.h"
#include "rf_spi.h"

#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.1.0"
#endif

/* --- 引脚(docs/02-hardware.md §3)------------------------------------ */
#define PIN_RF_KILL   8  /* 低有效:0 = KILL(禁止发射),1 = 允许。上电必须为 0 */
#define PIN_PAIR_BTN  38 /* 配对按键,低有效(内部上拉)。阶段 1 不消费 */

static const char *TAG = "rflab";

/* 临时打点(定位用):直接走 ROM 的 UART0 输出,不依赖任何驱动/控制台配置。 */
static void mark0(const char *m)
{
    while (*m) {
        esp_rom_output_putc(*m++);
    }
}

static SemaphoreHandle_t s_tx_lock;
static uart_dev_t       *s_uart; /* 轮询直驱用(见 transport_init) */

static rflab_frame_t s_rx_frame;
static char          s_json[RFLAB_USB_FRAME_MAX + 1];
static uint8_t       s_tx_buf[RFLAB_HDR_LEN + RFLAB_USB_FRAME_MAX];

static bool s_hello_done;
static char s_device_id[16];
static char s_client_id[PAIRING_CLIENT_ID_MAX + 1];

/* 三模块探测结果(阶段 2 第一步)。CAPS.modules[].present 由此派生 ——
 * ⚠️ 这是"真的探过了"的 false,与阶段 1 的"根本没探"不是一回事。 */
static rf_probe_t s_probe;
static rf_wire_t  s_wire;   /* MISO 空闲电平诊断,见 rf_spi.h */

/* ------------------------------------------------------------------ */
/* 输出:UART0(CH343P)上**只有帧**。没有日志混流 —— 控制台已关闭。      */
/* ------------------------------------------------------------------ */

static void tx_raw(const void *buf, size_t n)
{
    if (n == 0) {
        return;
    }
    /* 走 ROM 的 UART0 输出:这条路径在本板**已实测可用**(启动横幅就是它打的),
     * 而 IDF uart 驱动在本板当前状态下 RX/TX 都不动(见 transport_init 注释)。 */
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < n; i++) {
        esp_rom_output_putc((char)p[i]);
    }
    xSemaphoreGive(s_tx_lock);
}

static void send_json(uint8_t type, uint8_t seq, const cJSON *root)
{
    char *txt = cJSON_PrintUnformatted((cJSON *)root);
    if (txt == NULL) {
        return;
    }
    size_t n = rflab_frame_encode(s_tx_buf, sizeof(s_tx_buf), type, seq, txt,
                                  strlen(txt));
    cJSON_free(txt);
    if (n == 0) {
        /* 编码失败 = 载荷超过 usb_frame_max。禁止截断(§2.2.2)。 */
        ESP_LOGE(TAG, "帧编码失败(超长)");
        return;
    }
    tx_raw(s_tx_buf, n);
}

static void send_error(uint8_t seq, const char *code, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", msg);
    cJSON_AddNumberToObject(root, "seq_ref", seq);
    send_json(MSG_ERROR, seq, root);
    cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */

static void copy_payload_json(const rflab_frame_t *f)
{
    size_t n = (f->len < RFLAB_USB_FRAME_MAX) ? f->len : RFLAB_USB_FRAME_MAX;
    memcpy(s_json, f->payload, n);
    s_json[n] = '\0';
}

static void handle_hello(const rflab_frame_t *f)
{
    copy_payload_json(f);
    cJSON *root = cJSON_Parse(s_json);
    if (root == NULL) {
        /* ⚠️ 契约 §6 没有"载荷格式错"这一错误码,这里用 unsupported 并说明原因。
         *    这是一个已知缺口,已在报告中记录,不要据此发明新 code。 */
        send_error(f->seq, "unsupported", "HELLO 载荷不是合法 JSON");
        return;
    }

    cJSON *j_min  = cJSON_GetObjectItemCaseSensitive(root, "proto_min");
    cJSON *j_max  = cJSON_GetObjectItemCaseSensitive(root, "proto_max");
    cJSON *j_cid  = cJSON_GetObjectItemCaseSensitive(root, "client_id");
    cJSON *j_pair = cJSON_GetObjectItemCaseSensitive(root, "pairing");

    if (!cJSON_IsNumber(j_min) || !cJSON_IsNumber(j_max)) {
        send_error(f->seq, "version_mismatch", "缺少 proto_min/proto_max");
        cJSON_Delete(root);
        return;
    }
    if (j_min->valueint > (int)RFLAB_VER || j_max->valueint < (int)RFLAB_VER) {
        send_error(f->seq, "version_mismatch", "协议版本区间不含 1");
        cJSON_Delete(root);
        return;
    }

    if (cJSON_IsString(j_cid)) {
        strncpy(s_client_id, j_cid->valuestring, PAIRING_CLIENT_ID_MAX);
        s_client_id[PAIRING_CLIENT_ID_MAX] = '\0';
    }

    bool want_pair = cJSON_IsTrue(j_pair);
    bool pair_ok   = true;
    bool first_bond = false;
    const char *pair_error = NULL;

    if (want_pair) {
        if (!pairing_is_bonded()) {
            /* 首次配对。USB 是可信链路,故不看 pair_code(契约 §7.1(1))。 */
            if (pairing_create(s_client_id) == ESP_OK) {
                first_bond = true;
            } else {
                pair_ok = false;
                pair_error = "rng_failed";
            }
        } else if (pairing_client_matches(s_client_id)) {
            pair_ok = true; /* 已绑定且同一 client:不重发 secret */
        } else {
            pair_ok = false;
            /* 换绑需要设备端物理按键(§7.1(1))。阶段 1 尚未实现按键消费,
             * 所以此处如实返回 already_bound,而不是"静默改绑"。 */
            pair_error = "already_bound";
        }
    }

    cJSON *ack = cJSON_CreateObject();
    cJSON_AddStringToObject(ack, "fw_version", RFLAB_FW_VERSION);
    cJSON_AddNumberToObject(ack, "proto", RFLAB_VER);
    cJSON_AddStringToObject(ack, "device_id", s_device_id);
    cJSON_AddStringToObject(ack, "chip", "esp32s3");
    cJSON_AddNumberToObject(ack, "uptime_ms",
                            (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(ack, "last_accepted_seq", pairing_last_seq());

    if (want_pair) {
        cJSON_AddBoolToObject(ack, "pair_ok", pair_ok);
        if (pair_ok) {
            cJSON_AddBoolToObject(ack, "first_bond", first_bond);
            if (first_bond) {
                char b64[PAIRING_SECRET_B64_LEN + 1];
                if (pairing_secret_b64url(b64) == ESP_OK) {
                    /* ⚠️ 仅 USB 可信链路上出现,且只出现这一次。 */
                    cJSON_AddStringToObject(ack, "pairing_secret", b64);
                }
            }
        } else if (pair_error != NULL) {
            cJSON_AddStringToObject(ack, "pair_error", pair_error);
        }
    }

    send_json(MSG_HELLO_ACK, f->seq, ack);
    cJSON_Delete(ack);
    cJSON_Delete(root);

    if (!want_pair || pair_ok) {
        s_hello_done = true;
        led_set_state(LED_STATE_IDLE);
        ESP_LOGI(TAG, "HELLO 完成,pairing=%d bond=%d", (int)want_pair, (int)first_bond);
    }
}

static void add_band(cJSON *bands, double lo, double hi)
{
    cJSON *band = cJSON_CreateArray();
    cJSON_AddItemToArray(band, cJSON_CreateNumber(lo));
    cJSON_AddItemToArray(band, cJSON_CreateNumber(hi));
    cJSON_AddItemToArray(bands, band);
}

static cJSON *make_module(int slot, const char *chip, double lo, double hi, int tx_max)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddNumberToObject(m, "slot", slot);
    cJSON_AddStringToObject(m, "chip", chip);
    /* ✅ 阶段 2 起 present 来自**真实探测**(读芯片 ID,见 rf_probe.c):
     *    M1 = Si4463 GET_PART_INFO(CTS=0xFF 且 PART=0x4463)
     *    M2 = CC1101 PARTNUM=0x00 且 VERSION=0x14/0x04
     *    M3 = nRF24L01 CONFIG 复位值 0x08
     *    ⚠️ false 是"探过但没探到",不再是阶段 1 的"根本没探"。 */
    cJSON_AddBoolToObject(m, "present", s_probe.m[slot - 1].present);

    cJSON *bands = cJSON_CreateArray();
    add_band(bands, lo, hi); /* 模块级频段(非芯片级,§4 警示) */
    cJSON_AddItemToObject(m, "bands", bands);

    cJSON_AddNumberToObject(m, "tx_max_dbm", tx_max);
    cJSON_AddBoolToObject(m, "tx_allowed", false); /* 白名单为空 = 禁止一切发射 */
    return m;
}

static void handle_get_caps(const rflab_frame_t *f)
{
    if (!s_hello_done) {
        send_error(f->seq, "not_ready", "尚未完成 HELLO");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "proto", RFLAB_VER);
    cJSON_AddStringToObject(root, "fw", RFLAB_FW_VERSION);
    cJSON_AddStringToObject(root, "device_id", s_device_id);
    cJSON_AddNumberToObject(root, "active_slot", 1);

    cJSON *transports = cJSON_CreateArray();
    cJSON_AddItemToArray(transports, cJSON_CreateString("usb"));
    cJSON_AddItemToObject(root, "transports", transports);
    cJSON_AddStringToObject(root, "active_transport", "usb");

    cJSON *modules = cJSON_CreateArray();
    cJSON_AddItemToArray(modules, make_module(1, "si4463",  425000000.0,  525000000.0,   20));
    cJSON_AddItemToArray(modules, make_module(2, "cc1101",  387000000.0,  464000000.0,   10));
    cJSON_AddItemToArray(modules, make_module(3, "nrf24l01p", 2400000000.0, 2525000000.0, 20));
    cJSON_AddItemToObject(root, "modules", modules);

    cJSON *policy = cJSON_CreateObject();
    cJSON_AddItemToObject(policy, "tx_whitelist", cJSON_CreateArray());
    cJSON_AddNumberToObject(policy, "tx_power_cap_dbm", 20);
    cJSON_AddBoolToObject(policy, "tx_requires_token", true);
    cJSON_AddItemToObject(root, "policy", policy);

    cJSON *limits = cJSON_CreateObject();
    cJSON_AddNumberToObject(limits, "ble_frag_total_max", 15);
    cJSON_AddNumberToObject(limits, "ble_frame_max", 3600);
    cJSON_AddNumberToObject(limits, "usb_frame_max", RFLAB_USB_FRAME_MAX);
    cJSON_AddNumberToObject(limits, "max_edges", 16384);
    cJSON_AddNumberToObject(limits, "stream_rate_hz", 50);
    cJSON_AddNumberToObject(limits, "arm_timeout_ms", 5000);
    cJSON_AddItemToObject(root, "limits", limits);

    send_json(MSG_CAPS, f->seq, root);
    cJSON_Delete(root);
}

static void handle_ping(const rflab_frame_t *f)
{
    copy_payload_json(f);
    cJSON *root = cJSON_Parse(s_json);
    cJSON *pong = cJSON_CreateObject();

    if (cJSON_IsObject(root)) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "t_ms");
        if (cJSON_IsNumber(t)) {
            cJSON_AddNumberToObject(pong, "t_ms", t->valuedouble);
        }
    }
    if (cJSON_GetObjectItemCaseSensitive(pong, "t_ms") == NULL) {
        /* 没带 t_ms 也回一个,让 App 至少能拿到 RTT 的下半段。 */
        cJSON_AddNumberToObject(pong, "t_ms", 0);
    }
    send_json(MSG_PONG, f->seq, pong);
    cJSON_Delete(pong);
    if (root != NULL) {
        cJSON_Delete(root);
    }
}

static void dispatch(const rflab_frame_t *f)
{
    led_note_link_activity();

    if (f->type & 0x80) {
        /* 二进制数据帧(DATA_CHUNK 等)本批未实现。 */
        send_error(f->seq, "unsupported", "二进制帧本批未实现");
        return;
    }

    switch (f->type) {
    case MSG_HELLO:
        handle_hello(f);
        break;
    case MSG_GET_CAPS:
        handle_get_caps(f);
        break;
    case MSG_PING:
        handle_ping(f);
        break;
    default:
        send_error(f->seq, "unsupported", "未知消息类型");
        break;
    }
}

/* ------------------------------------------------------------------ */

static void rf_kill_assert_at_boot(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_RF_KILL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    /* 低有效 ⇒ 0 = KILL(禁止发射)。这是**安全默认**,不是可选项。
     * ⚠️ 复位到本语句之间引脚是输入态,靠载板外部下拉保证为 KILL(见接线图 §6)。 */
    gpio_set_level(PIN_RF_KILL, 0);

    /* 配对按键:阶段 1 只做输入配置,不消费事件(避免造出永不执行的分支)。 */
    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << PIN_PAIR_BTN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);
}


/* 2026-10-25 现场已按接线文档把 AS10 的 SDN(模块第 2 脚)直接接到 **GND**,
 *   GPIO18 归还给 M1 IRQ(本阶段探测用不到中断,先不配置)。
 *   上一版的 GPIO18→SDN workaround 已删除 —— SDN 接地后若再把 GPIO18 驱动为输出低,
 *   会与模块的 nIRQ 输出顶牛。 */

static void make_device_id(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "RFLAB-%02X%02X", mac[4], mac[5]);
}

/* 控制通道初始化:UART0(CH343P)@921600 8N1。
 * 返回 esp_err_t 而不是 ESP_ERROR_CHECK:装配失败时设备仍要能点灯/进故障态,
 * 不能因为一条链路的问题让"安全默认"也变得不可观测。 */
static esp_err_t transport_init(void)
{
    /* 控制通道 = CH343P ↔ UART0。
     *
     * ⚠️ 为什么不用 IDF 的 uart 驱动(2026-10-22 实测):
     *    `uart_driver_install` 返回 0、`uart_write_bytes` 返回字节数、`uart_wait_tx_done` 返回 0,
     *    但**线上一个字节都没有**,RX 也收不到 —— 现象指向"驱动 ISR 没被服务",
     *    而 ROM 的 UART 输出始终正常。为避免在根因未明时把产品堵死,本版走 ROM 路径:
     *      TX = esp_rom_output_putc()
     *      RX = esp_rom_output_rx_one_char() 轮询
     *    波特率:2026-10-25 实测 ROM 路径同样能跑 **921600**(此前"只能 115200"的判断是错的
     *    —— 波特率由 uart_param_config 决定,ROM 输出不限制它)。注意复位后 ROM bootloader
     *    自己的横幅仍是 115200,进 App 后才切到 921600。 */
    /* 配置 UART0 的引脚与格式(不安装驱动):RX 必须被显式路由到 GPIO44,
     * 否则 uart_ll 轮询读到的永远是空 FIFO。 */
    const uart_config_t cfg = {
        .baud_rate  = RFLAB_UART_BAUD, /* 2026-10-25 实测:ROM 路径也能跑 921600 */
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(RFLAB_UART_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(RFLAB_UART_PORT, PIN_UART0_TX, PIN_UART0_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    s_uart = UART_LL_GET_HW(RFLAB_UART_PORT);
    /* 清掉复位阶段残留的字节,保证首帧从干净边界开始。 */
    uint8_t drop;
    for (int i = 0; i < 256; i++) {
        if (esp_rom_output_rx_one_char(&drop) != 0) {
            break;
        }
    }
    return ESP_OK;
}

/* 启动横幅。⚠️ 为什么必须存在:控制台与 IDF 日志在本版是关闭的
 * (协议独占 UART0),若再没有这几行,设备出问题时就是"插上毫无反应",
 * 无法区分"没启动 / 启动失败 / 链路没起来"。
 * 格式与波特率都与协议一致(921600、行首 '#'):App 侧解析器只在 MAGIC 处开始,
 * 这几行会被当成噪声跳过;而人用串口工具打开就能看见。
 * ⚠️ 只允许出现在启动阶段。运行期**不得**再往这条流里写文本。 */

/* 线检结论:cs_pu == cs_pd 才表示"MISO 被驱动",否则即使选中也悬空。 */
static const char *drive_verdict(const rf_mod_t *m)
{
    if (m->cs_pu != m->cs_pd) {
        return "FLOATING(选中后仍无驱动:CS/MISO 没接或模块没上电)";
    }
    return m->cs_pu ? "driven-HIGH" : "driven-LOW";
}

static void send_banner(bool pairing_ok)
{
    char buf[1024];
    const char *wire_v = (s_wire.miso_pd == 0 && s_wire.miso_pu == 1)
                             ? "floating(MISO 未接到任何驱动)"
                             : (s_wire.miso_pd ? "driven-HIGH(被强拉高)" : "driven-LOW(被拉低)");
    int n = snprintf(buf, sizeof(buf),
                     "\r\n# RF-Lab fw=%s proto=%d device=%s boot\r\n"
                     "# carrier=CH343P->UART0 baud=%d (native USB: never enumerated)\r\n"
                     "# wire MISO 14: pd=%d pu=%d -> %s\r\n"
                     "# RF_KILL=asserted(禁止发射) pairing:%s\r\n"
                     "# probe M1 si4463   cts=0x%02X part=0x%04X present=%d\r\n"
                     "# line  M1 cs_pu=%d cs_pd=%d -> %s\r\n"
                     "# line  M2 cs_pu=%d cs_pd=%d -> %s\r\n"
                     "# spi   M1 pullup=0x%02X pulldown=0x%02X -> %s\r\n"
                     "# spi   M2 pullup=0x%02X pulldown=0x%02X -> %s\r\n"
                     "# probe M2 cc1101   partnum=0x%02X version=0x%02X present=%d\r\n"
                     "# probe M3 nrf24l01 config=0x%02X status=0x%02X present=%d\r\n"
                     "# loop  MOSI13->MISO14 hi=%d lo=%d -> %s\r\n",
                     RFLAB_FW_VERSION, RFLAB_VER, s_device_id,
                     (int)RFLAB_UART_BAUD,
                     s_wire.miso_pd, s_wire.miso_pu, wire_v,
                     pairing_ok ? "ok" : "FAILED",
                     s_probe.m[0].id0,
                     (unsigned)(s_probe.m[0].id1 | (s_probe.m[0].id2 << 8)),
                     (int)s_probe.m[0].present,
                     s_probe.m[0].cs_pu, s_probe.m[0].cs_pd, drive_verdict(&s_probe.m[0]),
                     s_probe.m[1].cs_pu, s_probe.m[1].cs_pd, drive_verdict(&s_probe.m[1]),
                     s_probe.m[0].spi_up, s_probe.m[0].spi_down,
                     (s_probe.m[0].spi_up != s_probe.m[0].spi_down) ? "读回随上下拉变"
                                                                    : "读回不随上下拉变",
                     s_probe.m[1].spi_up, s_probe.m[1].spi_down,
                     (s_probe.m[1].spi_up != s_probe.m[1].spi_down) ? "读回随上下拉变"
                                                                    : "读回不随上下拉变",
                     s_probe.m[1].id0, s_probe.m[1].id1,
                     (int)s_probe.m[1].present,
                     s_probe.m[2].id0, s_probe.m[2].id1,
                     (int)s_probe.m[2].present,
                     s_probe.loop_hi, s_probe.loop_lo,
                     (s_probe.loop_hi == 1 && s_probe.loop_lo == 0)
                         ? "PASS(13-14 已短接,ESP32 侧 SPI 正常)"
                         : "FAIL(未短接 13-14,或 GPIO13/14 损坏)");
    if (n <= 0) {
        return;
    }
    size_t off = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

    /* M2 的三个 RSSI 采样点:证明"调谐 + 接收链路"真的在工作,不只是 SPI 通。 */
    for (int i = 0; i < RF_PROBE_POINTS && off + 64 < sizeof(buf); i++) {
        if (!s_probe.m[1].rssi_ok[i]) {
            continue;
        }
        int k = snprintf(buf + off, sizeof(buf) - off,
                         "# rssi %.3fMHz = %d dBm (raw=0x%02X)\r\n",
                         (double)s_probe.m[1].freq_hz[i] / 1e6,
                         s_probe.m[1].rssi_dbm[i], s_probe.m[1].rssi_raw[i]);
        if (k > 0) {
            off += (size_t)k;
        }
    }

    if (off + 16 < sizeof(buf)) {
        int k = snprintf(buf + off, sizeof(buf) - off, "# ready\r\n");
        if (k > 0) {
            off += (size_t)k;
        }
    }

    tx_raw(buf, off);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot: fw=%s proto=%d", RFLAB_FW_VERSION, RFLAB_VER);

    /* 1. 先立安全默认:RF_KILL 拉低。放在任何可能耗时/阻塞的初始化之前。 */
    rf_kill_assert_at_boot();
    ESP_LOGI(TAG, "step1: RF_KILL asserted");

    s_tx_lock = xSemaphoreCreateMutex();

    /* 2. 本地指示:先写黑(KILL 态),再按状态切换。 */
    led_init();
    ESP_LOGI(TAG, "step2a: led_init ok");
#ifdef RFLAB_LED_SELFTEST
    /* 载板正品检查(TC-106 / R-18):能看见红→绿→蓝 = 2812 信号已焊。 */
    led_selftest();
    ESP_LOGI(TAG, "step2b: led_selftest ok");
#endif
    led_set_state(LED_STATE_KILL);
    led_start();
    ESP_LOGI(TAG, "step2c: led task started");

    make_device_id();
    ESP_LOGI(TAG, "step2d: device_id=%s", s_device_id);

    /* 3. 控制通道(本批唯一链路):CH343P → UART0。 */
    mark0("[0]start\r\n");
    {
        /* 诊断:UART0 RX(GPIO44)空闲时应为高电平。若被外部拉低/拉高,
         * 说明有东西接在这条线上(接线冲突)。 */
        gpio_config_t in = {
            .pin_bit_mask = (1ULL << PIN_UART0_RX),
            .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&in);
        vTaskDelay(pdMS_TO_TICKS(2));
        mark0(gpio_get_level(PIN_UART0_RX) ? "[rx-idle-high]\r\n" : "[rx-idle-LOW]\r\n");
    }
    esp_err_t err = transport_init();
    if (err != ESP_OK) {
        led_set_state(LED_STATE_FAULT);
    }

    /* 4. 配对状态(内部含 NVS 初始化)。 */
    mark0("[1]transport\r\n");
    bool pairing_ok = (pairing_init() == ESP_OK);
    if (!pairing_ok) {
        led_set_state(LED_STATE_FAULT);
    }

    /* 5. 射频总线 + 三模块探测(阶段 2 第一步:先证明接线通,再谈驱动)。
     *    ⚠️ 探测全程只读、不发射;未连接的模块报 present=false 是预期。 */
    mark0("[2]pairing\r\n");
    /* SDN 已于 2026-10-25 接到 GND,这里不再驱动 GPIO18(见上面的说明)。 */
    /* 先判 MISO 有没有接到东西,再做 SPI —— 否则三块全 0xFF 时无从分辨原因。 */
    rf_wire_check(&s_wire);
    esp_err_t rf_err = rf_spi_init();
    mark0("[3]spi\r\n");
    if (rf_err == ESP_OK) {
        rf_probe_run(&s_probe);
    } else {
        led_set_state(LED_STATE_FAULT);
    }

    mark0("[4]probe done\r\n");
    /* 6. 启动横幅(此时的状态就是真实初始化结果)。 */
    if (err == ESP_OK) {
        send_banner(pairing_ok);
    }

    mark0("[5]banner sent\r\n");
    rflab_rx_t rx;
    rflab_rx_init(&rx);

    for (;;) {
        bool got = false;
        for (int guard = 0; guard < 256; guard++) {
            /* 直接读 UART0 的 RX FIFO:不依赖驱动 ISR,也不依赖 ROM 控制台的 RX 通道。 */
            uint8_t c;
            if (uart_ll_get_rxfifo_len(s_uart) == 0) {
                break;
            }
            uart_ll_read_rxfifo(s_uart, &c, 1);
            got = true;
            int rc = rflab_rx_feed(&rx, c, &s_rx_frame);
            if (rc == RFLAB_RX_FRAME) {
                dispatch(&s_rx_frame);
            } else if (rc == RFLAB_RX_BAD_VER) {
                send_error(0, "version_mismatch", "帧头 VER != 1");
            } else if (rc == RFLAB_RX_TOO_BIG) {
                send_error(0, "frame_too_large", "LEN 超过 usb_frame_max");
            }
        }
        if (!got) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}
