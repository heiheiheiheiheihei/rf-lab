/*
 * main.c — RF-Lab 固件入口(本批:USB-only,阶段 1 骨架)
 *
 * 本文件只做四件事:
 *   1. 上电即把 RF_KILL 拉低(**硬件发射门控,低有效** = 默认禁止发射)
 *   2. 初始化板载 WS2812 并按硬件文档 §2.7 的指示语显示
 *   3. 在 USB Serial/JTAG 上跑契约 §2 的帧协议
 *   4. 实现 §3.1 的四条消息:HELLO / HELLO_ACK / GET_CAPS / CAPS / PING / PONG / ERROR
 *
 * 明确**没做**(写出来是为了避免"看起来做了"):
 *   - 射频模块驱动与探测 ⇒ CAPS.modules[].present 一律 false(**未探测**,不是"确认没有")
 *   - 扫频 / 捕获 / 发射 / 门禁 / 令牌校验(阶段 2–4)
 *   - `RF_KILL` 的软件切换:契约里**还没有这个字段**(已知归属缺口,
 *     见接线文档 §6)。⚠️ 不要为此自创一个消息类型 —— 那会让 App 侧错位。
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "frame.h"
#include "led.h"
#include "pairing.h"

#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.1.0"
#endif

/* --- 引脚(硬件文档 §2.3)--------------------------------------------- */
#define PIN_RF_KILL   8  /* 低有效:0 = KILL(禁止发射),1 = 允许。上电必须为 0 */
#define PIN_PAIR_BTN  38 /* 配对按键,低有效(内部上拉)。阶段 1 不消费 */

static const char *TAG = "rflab";

static SemaphoreHandle_t s_tx_lock;

static rflab_frame_t s_rx_frame;
static char          s_json[RFLAB_USB_FRAME_MAX + 1];
static uint8_t       s_tx_buf[RFLAB_HDR_LEN + RFLAB_USB_FRAME_MAX];

static bool s_hello_done;
static char s_device_id[16];
static char s_client_id[PAIRING_CLIENT_ID_MAX + 1];

/* ------------------------------------------------------------------ */
/* 输出:帧与日志共用同一条字节流。日志用 "# " 前缀,App 靠 MAGIC 重同步。 */
/* ------------------------------------------------------------------ */

static void tx_raw(const void *buf, size_t n)
{
    if (n == 0) {
        return;
    }
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    usb_serial_jtag_write_bytes(buf, n, portMAX_DELAY);
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
    /* ⚠️ 阶段 1 没有射频驱动,也**没有探测过**。契约 §4 要求"按实际装的前端上报",
     *    所以这里必须报 false —— 把未探测写成 true 会让 App 及后续用例
     *    建立在一个从未验证过的前提上。 */
    cJSON_AddBoolToObject(m, "present", false);

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

static void make_device_id(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "RFLAB-%02X%02X", mac[4], mac[5]);
}

void app_main(void)
{
    /* 1. 先立安全默认:RF_KILL 拉低。放在任何可能耗时/阻塞的初始化之前。 */
    rf_kill_assert_at_boot();

    s_tx_lock = xSemaphoreCreateMutex();

    /* 2. 本地指示:先写黑(KILL 态),再按状态切换。 */
    led_init();
#ifdef RFLAB_LED_SELFTEST
    /* 载板正品检查(TC-106 / R-18):能看见红→绿→蓝 = 2812 信号已焊。 */
    led_selftest();
#endif
    led_set_state(LED_STATE_KILL);
    led_start();

    make_device_id();

    /* 3. USB Serial/JTAG:本批唯一的控制通道。
     *    控制台也落在这个外设上(见 sdkconfig.defaults),故驱动可能已被 VFS 安装;
     *    重复安装会返回 ESP_ERR_INVALID_STATE,那不是错误。 */
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 4096,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&usb_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "USB Serial/JTAG 驱动安装失败: %s", esp_err_to_name(err));
    }

    /* 4. 配对状态(内部含 NVS 初始化)。 */
    if (pairing_init() != ESP_OK) {
        ESP_LOGE(TAG, "配对状态初始化失败");
        led_set_state(LED_STATE_FAULT);
    }

    /* 启动横幅。刻意用 "# " 前缀:它与帧(MAGIC 0x52)不会混淆。 */
    printf("\r\n# RF-Lab fw=%s proto=%d device=%s boot\r\n",
           RFLAB_FW_VERSION, RFLAB_VER, s_device_id);
    printf("# RF_KILL=asserted(禁止发射)  modules: not probed\r\n");
    printf("# ready\r\n");

    rflab_rx_t rx;
    rflab_rx_init(&rx);

    static uint8_t buf[512];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            int rc = rflab_rx_feed(&rx, buf[i], &s_rx_frame);
            if (rc == RFLAB_RX_FRAME) {
                dispatch(&s_rx_frame);
            } else if (rc == RFLAB_RX_BAD_VER) {
                send_error(0, "version_mismatch", "帧头 VER != 1");
            } else if (rc == RFLAB_RX_TOO_BIG) {
                send_error(0, "frame_too_large", "LEN 超过 usb_frame_max");
            }
        }
    }
}
