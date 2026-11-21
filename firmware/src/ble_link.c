#include "ble_link.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "link.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern void ble_store_config_init(void);

#ifndef RFLAB_FW_VERSION
#define RFLAB_FW_VERSION "0.6.0"
#endif
#ifndef RFLAB_BUILD_ID
#define RFLAB_BUILD_ID "dev"
#endif
#ifndef RFLAB_UI_HASH
#define RFLAB_UI_HASH "unknown"
#endif

/* 128-bit UUID,小端字节序(BLE_UUID128_INIT 的约定)。 */
#define SVC_BYTES  0x31, 0x42, 0x41, 0x4c, 0x46, 0x52, 0x55, 0x9a, \
                   0x8e, 0x4b, 0x1f, 0x7c, 0x01, 0x00, 0x3d, 0x5a
#define RX_BYTES   0x31, 0x42, 0x41, 0x4c, 0x46, 0x52, 0x55, 0x9a, \
                   0x8e, 0x4b, 0x1f, 0x7c, 0x02, 0x00, 0x3d, 0x5a
#define TX_BYTES   0x31, 0x42, 0x41, 0x4c, 0x46, 0x52, 0x55, 0x9a, \
                   0x8e, 0x4b, 0x1f, 0x7c, 0x03, 0x00, 0x3d, 0x5a

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(SVC_BYTES);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(RX_BYTES);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(TX_BYTES);

static uint16_t s_tx_handle;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_subscribed;
static volatile bool s_authed;
static volatile bool s_enabled = true;
static volatile bool s_synced;
static volatile bool s_hello_pending;
static volatile int s_bad_pin;
static uint8_t s_own_addr_type;

static char s_name[32];
static char s_pin[5];

#define BLE_RX_LINE 128
#define BLE_TERM_USER 0x13 /* BLE_ERR_REM_USER_CONN_TERM, NimBLE 把它放在 .c 里 */
static char s_rx_line[BLE_RX_LINE];
static size_t s_rx_len;

/* 单消费者(TX task),静态 buffer 省栈。 */
static char s_line[APP_CODES_JSON_MAX + 160];
static char s_codes[APP_CODES_JSON_MAX];
static char s_ev[LINK_LINE_MAX];
static SemaphoreHandle_t s_tx_lock;

/* host task 只入队,不在 GATT 回调里直接 notify(避免与 TX task 的 host 锁互相等待)。 */
static char s_pending[192];
static volatile bool s_pending_set;
static portMUX_TYPE s_pending_mux = portMUX_INITIALIZER_UNLOCKED;

static int gap_event(struct ble_gap_event *event, void *arg);
static int start_adv(void);

static bool notify_bytes(const uint8_t *data, size_t len)
{
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE || !s_subscribed || len == 0) {
        return false;
    }

    uint16_t mtu = ble_att_mtu(conn);
    size_t chunk = (mtu > 3) ? (size_t)(mtu - 3) : 20;
    if (chunk > 200) {
        chunk = 200;
    }

    if (s_tx_lock) {
        xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    }

    bool ok = true;
    size_t sent = 0;
    int retries = 0;
    while (sent < len) {
        size_t n = len - sent;
        if (n > chunk) {
            n = chunk;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + sent, (uint16_t)n);
        if (!om) {
            ok = false;
            break;
        }
        int rc = ble_gatts_notify_custom(conn, s_tx_handle, om);
        if (rc != 0) {
            if (rc == BLE_HS_ENOTSUP) {
                os_mbuf_free_chain(om);
            }
            if (rc == BLE_HS_ENOMEM && ++retries < 25) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            ok = false;
            break;
        }
        retries = 0;
        sent += n;
        vTaskDelay(pdMS_TO_TICKS(3));
    }

    if (s_tx_lock) {
        xSemaphoreGive(s_tx_lock);
    }
    return ok;
}

static void notify_line(const char *line)
{
    if (!line || !*line) {
        return;
    }
    char buf[LINK_LINE_MAX + 2];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n > 0 && n < (int)sizeof(buf)) {
        notify_bytes((const uint8_t *)buf, (size_t)n);
    }
}

/* 从 NimBLE host 任务调用:只复制到 pending,由 TX task 发送。 */
static void queue_line(const char *line)
{
    if (!line) {
        return;
    }
    portENTER_CRITICAL(&s_pending_mux);
    snprintf(s_pending, sizeof(s_pending), "%s", line);
    s_pending_set = true;
    portEXIT_CRITICAL(&s_pending_mux);
}

static void send_hello(void)
{
    snprintf(s_line, sizeof(s_line),
             "{\"t\":\"hello\",\"fw\":\"%s\",\"build\":\"%s\",\"ui\":\"%s\","
             "\"proto\":1,\"name\":\"%s\",\"auth\":%d}",
             RFLAB_FW_VERSION, RFLAB_BUILD_ID, RFLAB_UI_HASH, app_name(), s_authed ? 1 : 0);
    notify_line(s_line);
}

static void handle_auth(const char *pin)
{
    if (pin && strcmp(pin, s_pin) == 0) {
        s_authed = true;
        s_bad_pin = 0;
        s_hello_pending = true; /* 重新发 hello(auth=1)+ 快照 */
        queue_line("{\"t\":\"auth\",\"ok\":1}");
        app_mark_state_dirty();
        return;
    }

    s_bad_pin++;
    char msg[64];
    snprintf(msg, sizeof(msg), "{\"t\":\"auth\",\"ok\":0,\"left\":%d}",
             s_bad_pin >= 5 ? 0 : 5 - s_bad_pin);
    queue_line(msg);
    if (s_bad_pin >= 5) {
        s_bad_pin = 0;
        if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conn, BLE_TERM_USER);
        }
    }
}

static void handle_command_line(char *line)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = 0;
    }
    if (n == 0) {
        return;
    }

    if (strncmp(line, "AUTH ", 5) == 0) {
        handle_auth(line + 5);
        return;
    }
    if (strcmp(line, "AUTH") == 0) {
        queue_line("{\"t\":\"error\",\"line\":\"usage: AUTH <pin>\"}");
        return;
    }
    if (!s_authed) {
        queue_line("{\"t\":\"error\",\"line\":\"AUTH required\"}");
        return;
    }

    if (app_submit_cmd(line, APP_SRC_BLE) != 0) {
        queue_line("{\"t\":\"error\",\"line\":\"queue full\"}");
    }
}

static void rx_feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            s_rx_line[s_rx_len] = 0;
            handle_command_line(s_rx_line);
            s_rx_len = 0;
        } else if (c != '\r') {
            if (s_rx_len < sizeof(s_rx_line) - 1) {
                s_rx_line[s_rx_len++] = c;
            } else {
                /* 超长行:丢弃并回错误,避免半截命令被执行。 */
                s_rx_len = 0;
                queue_line("{\"t\":\"error\",\"line\":\"line too long\"}");
            }
        }
    }
}

static int chr_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t total = OS_MBUF_PKTLEN(ctxt->om);
        if (total > 256) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t buf[256];
        uint16_t copied = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, total, &copied) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        rx_feed(buf, copied);
        return 0;
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_handle,
            },
            {0},
        },
    },
    {0},
};

static int start_adv(void)
{
    if (!s_enabled || !s_synced || s_conn != BLE_HS_CONN_HANDLE_NONE) {
        return 0;
    }

    struct ble_hs_adv_fields adv = {0};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.name = (uint8_t *)s_name;
    adv.name_len = (uint8_t)strlen(s_name);
    adv.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv);
    if (rc != 0) {
        return rc;
    }

    struct ble_hs_adv_fields rsp = {0};
    rsp.uuids128 = &s_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        return rc;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);
    return ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            s_subscribed = false;
            s_authed = false;
            s_bad_pin = 0;
            s_rx_len = 0;
        } else {
            start_adv();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        s_authed = false;
        s_bad_pin = 0;
        s_rx_len = 0;
        start_adv();
        app_mark_state_dirty();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_adv();
        return 0;

    case BLE_GAP_EVENT_MTU:
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            s_subscribed = event->subscribe.cur_notify != 0;
            if (s_subscribed) {
                s_hello_pending = true;
            }
            app_mark_state_dirty();
        }
        return 0;

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    (void)reason;
    s_synced = false;
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        return;
    }
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        return;
    }
    s_synced = true;
    start_adv();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void tx_task(void *param)
{
    (void)param;
    uint32_t cursor = 0;
    uint32_t st_seq = 0;
    uint32_t cd_seq = 0;
    int64_t last_state = 0;

    for (;;) {
        if (s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed) {
            if (s_pending_set) {
                portENTER_CRITICAL(&s_pending_mux);
                snprintf(s_ev, sizeof(s_ev), "%s", s_pending);
                s_pending_set = false;
                portEXIT_CRITICAL(&s_pending_mux);
                notify_line(s_ev);
            }
            if (s_hello_pending) {
                s_hello_pending = false;
                send_hello();
                st_seq = 0;
                cd_seq = 0;
                cursor = link_head();
            }

            if (s_authed) {
                while (link_read(&cursor, s_ev, sizeof(s_ev), NULL) == 1) {
                    notify_line(s_ev);
                }

                int64_t now = app_now_ms();
                if (app_state_seq() != st_seq && now - last_state >= 200) {
                    app_copy_state(s_line, APP_STATE_JSON_MAX);
                    char tmp[APP_STATE_JSON_MAX + 64];
                    snprintf(tmp, sizeof(tmp), "{\"t\":\"state\",\"seq\":%lu,\"data\":%s}",
                             (unsigned long)app_state_seq(), s_line);
                    if (notify_bytes((const uint8_t *)tmp, strlen(tmp))) {
                        st_seq = app_state_seq();
                        last_state = now;
                    }
                }

                if (app_codes_seq() != cd_seq) {
                    app_copy_codes(s_codes, sizeof(s_codes));
                    snprintf(s_line, sizeof(s_line),
                             "{\"t\":\"codes\",\"seq\":%lu,\"items\":%s}",
                             (unsigned long)app_codes_seq(), s_codes);
                    if (notify_bytes((const uint8_t *)s_line, strlen(s_line))) {
                        cd_seq = app_codes_seq();
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t ble_link_start(const char *name, const char *pin)
{
    snprintf(s_name, sizeof(s_name), "%s", name ? name : "RF-Lab");
    snprintf(s_pin, sizeof(s_pin), "%s", pin ? pin : "0000");

    s_tx_lock = xSemaphoreCreateMutex();
    if (!s_tx_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_name);
    ble_store_config_init();

    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc != 0) {
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) {
        return ESP_FAIL;
    }

    nimble_port_freertos_init(host_task);
    if (xTaskCreate(tx_task, "ble_tx", 6144, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ble_link_is_on(void)
{
    return s_enabled;
}

bool ble_link_connected(void)
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE && s_subscribed;
}

bool ble_link_advertising(void)
{
    return s_synced && ble_gap_adv_active() != 0;
}

void ble_link_set_enabled(bool on)
{
    s_enabled = on;
    if (!on) {
        if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conn, BLE_TERM_USER);
        }
        ble_gap_adv_stop();
    } else {
        start_adv();
    }
    app_mark_state_dirty();
}
