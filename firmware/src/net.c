#include "net.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define NET_NS "net"
#define NET_KEY_SSID "ssid"
#define NET_KEY_PASS "pass"

#define CONNECT_TIMEOUT_US (15ULL * 1000 * 1000)
#define FALLBACK_DELAY_US (100ULL * 1000)

typedef enum {
    NET_MODE_AP = 0,
    NET_MODE_CONNECTING,
    NET_MODE_STA,
    NET_MODE_APSTA,
} net_mode_t;

static net_info_t s_info;
static SemaphoreHandle_t s_lock;
static bool s_started;
static net_mode_t s_mode = NET_MODE_AP;
static bool s_keep_ap_on_success;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_timer_handle_t s_connect_timer;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_fallback_timer;
static int s_retry;
static char s_ip[16] = "0.0.0.0";

static void connect_timeout_cb(void *arg);
static void retry_cb(void *arg);
static void fallback_cb(void *arg);

static void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void ip4_str(const esp_ip4_addr_t *a, char out[16])
{
    snprintf(out, 16, IPSTR, IP2STR(a));
}

static const char *mode_name(net_mode_t m)
{
    switch (m) {
    case NET_MODE_CONNECTING:
        return "connecting";
    case NET_MODE_STA:
        return "sta";
    case NET_MODE_APSTA:
        return "apsta";
    case NET_MODE_AP:
    default:
        return "ap";
    }
}

static bool mode_has_ap(net_mode_t m)
{
    return m == NET_MODE_AP || m == NET_MODE_APSTA;
}

static void refresh_ip_locked(void)
{
    if (s_info.sta_connected && s_info.sta_ip[0]) {
        snprintf(s_ip, sizeof(s_ip), "%s", s_info.sta_ip);
    } else if (s_info.ap_on && s_info.ap_ip[0]) {
        snprintf(s_ip, sizeof(s_ip), "%s", s_info.ap_ip);
    } else {
        snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
    }
}

static void set_mode_locked(net_mode_t m)
{
    s_mode = m;
    snprintf(s_info.mode, sizeof(s_info.mode), "%s", mode_name(m));
    s_info.ap_on = mode_has_ap(m);
    refresh_ip_locked();
}

static esp_err_t apply_mode(net_mode_t m)
{
    wifi_mode_t wm;

    switch (m) {
    case NET_MODE_CONNECTING:
    case NET_MODE_STA:
        wm = WIFI_MODE_STA;
        break;
    case NET_MODE_APSTA:
        wm = WIFI_MODE_APSTA;
        break;
    case NET_MODE_AP:
    default:
        wm = WIFI_MODE_AP;
        break;
    }

    esp_err_t e = esp_wifi_set_mode(wm);
    if (e != ESP_OK) {
        return e;
    }
    lock();
    set_mode_locked(m);
    unlock();
    app_mark_state_dirty();
    return ESP_OK;
}

static void stop_timer(esp_timer_handle_t t)
{
    if (t) {
        esp_timer_stop(t); /* not running → ESP_ERR_INVALID_STATE, ignore */
    }
}

static void arm_connect_timeout(void)
{
    if (!s_connect_timer) {
        return;
    }
    stop_timer(s_connect_timer);
    esp_timer_start_once(s_connect_timer, CONNECT_TIMEOUT_US);
}

static uint32_t next_retry_ms(void)
{
    static const uint32_t steps[] = {5000, 10000, 20000, 30000, 60000};
    uint32_t ms = steps[s_retry < 5 ? s_retry : 4];
    if (s_retry < 5) {
        s_retry++;
    }
    return ms;
}

static void arm_retry(uint32_t ms)
{
    if (!s_retry_timer) {
        return;
    }
    stop_timer(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)ms * 1000);
}

static bool load_sta(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    nvs_handle_t h;
    size_t sl = ssid_cap;
    size_t pl = pass_cap;

    ssid[0] = 0;
    pass[0] = 0;
    if (nvs_open(NET_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t e1 = nvs_get_str(h, NET_KEY_SSID, ssid, &sl);
    esp_err_t e2 = nvs_get_str(h, NET_KEY_PASS, pass, &pl);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && ssid[0] != 0;
}

static void save_sta(const char *ssid, const char *pass)
{
    nvs_handle_t h;

    if (nvs_open(NET_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, NET_KEY_SSID, ssid);
    nvs_set_str(h, NET_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
}

static void erase_sta(void)
{
    nvs_handle_t h;

    if (nvs_open(NET_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, NET_KEY_SSID);
    nvs_erase_key(h, NET_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
}

static void note(const char *l1, const char *l2)
{
    char ip[16];
    lock();
    snprintf(ip, sizeof(ip), "%s", s_ip);
    unlock();
    app_note_net_event(l1, l2 ? l2 : ip);
}

/* 15s 内没拿到 IP:从 connecting 退到 AP+STA;AP 立即可用。 */
static void connect_timeout_cb(void *arg)
{
    (void)arg;

    lock();
    bool connected = s_info.sta_connected;
    bool creds = s_info.has_creds;
    net_mode_t m = s_mode;
    unlock();

    if (connected || !creds || m != NET_MODE_CONNECTING) {
        return;
    }

    if (apply_mode(NET_MODE_APSTA) == ESP_OK) {
        note("AP FALLBACK", NULL);
        arm_retry(5000);
    }
}

/* 已连上 STA 后掉线:立即把 AP 打开,并继续后台重连。 */
static void fallback_cb(void *arg)
{
    (void)arg;

    lock();
    bool connected = s_info.sta_connected;
    bool creds = s_info.has_creds;
    net_mode_t m = s_mode;
    unlock();

    if (connected || !creds || m != NET_MODE_STA) {
        return;
    }

    if (apply_mode(NET_MODE_APSTA) == ESP_OK) {
        note("AP FALLBACK", NULL);
        arm_retry(5000);
    }
}

static void retry_cb(void *arg)
{
    (void)arg;

    lock();
    bool connected = s_info.sta_connected;
    bool creds = s_info.has_creds;
    net_mode_t m = s_mode;
    unlock();

    if (!s_started || connected || !creds) {
        return;
    }
    if (m == NET_MODE_CONNECTING || m == NET_MODE_STA || m == NET_MODE_APSTA) {
        esp_wifi_connect();
        arm_retry(next_retry_ms());
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == WIFI_EVENT_STA_START) {
        lock();
        bool creds = s_info.has_creds;
        net_mode_t m = s_mode;
        unlock();
        if (creds && m == NET_MODE_CONNECTING) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        lock();
        bool creds = s_info.has_creds;
        net_mode_t m = s_mode;
        s_info.sta_connected = false;
        s_info.sta_ip[0] = 0;
        s_info.rssi = 0;
        refresh_ip_locked();
        unlock();
        app_mark_state_dirty();

        if (!creds) {
            return;
        }
        if (m == NET_MODE_CONNECTING) {
            arm_retry(2000);
        } else if (m == NET_MODE_STA) {
            if (s_fallback_timer) {
                stop_timer(s_fallback_timer);
                esp_timer_start_once(s_fallback_timer, FALLBACK_DELAY_US);
            }
        } else if (m == NET_MODE_APSTA) {
            arm_retry(next_retry_ms());
        }
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        lock();
        s_info.ap_clients++;
        unlock();
        app_mark_state_dirty();
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        lock();
        if (s_info.ap_clients > 0) {
            s_info.ap_clients--;
        }
        unlock();
        app_mark_state_dirty();
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    char ip[16];
    ip4_str(&ev->ip_info.ip, ip);

    lock();
    s_info.sta_connected = true;
    snprintf(s_info.sta_ip, sizeof(s_info.sta_ip), "%s", ip);
    s_info.has_creds = true;
    if (s_mode == NET_MODE_CONNECTING) {
        set_mode_locked(s_keep_ap_on_success ? NET_MODE_APSTA : NET_MODE_STA);
    } else if (s_mode == NET_MODE_AP) {
        set_mode_locked(NET_MODE_APSTA);
    }
    refresh_ip_locked();
    bool ap_on = s_info.ap_on;
    s_retry = 0;
    unlock();

    stop_timer(s_connect_timer);
    stop_timer(s_retry_timer);
    stop_timer(s_fallback_timer);
    app_mark_state_dirty();
    note(ap_on ? "LAN+AP" : "LAN OK", ip);
}

esp_err_t net_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    bool have_sta = load_sta(ssid, sizeof(ssid), pass, sizeof(pass));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_ap_netif || !s_sta_netif) {
        return ESP_ERR_NO_MEM;
    }
    esp_netif_set_hostname(s_sta_netif, "rf-lab");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL));

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_info.ap_ssid, sizeof(s_info.ap_ssid), RFLAB_AP_PREFIX "%02X%02X", mac[4], mac[5]);
    snprintf(s_info.ap_ip, sizeof(s_info.ap_ip), "192.168.4.1");
    s_info.has_creds = have_sta;
    if (have_sta) {
        snprintf(s_info.sta_ssid, sizeof(s_info.sta_ssid), "%s", ssid);
    }

    wifi_config_t ap = {0};
    /* 显式 precision:SSID 最多 31 字符 + NUL,避免新版 GCC 的
     * -Werror=format-truncation 把「可能 33 字节」判成错误。 */
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%.*s",
             (int)sizeof(ap.ap.ssid) - 1, s_info.ap_ssid);
    ap.ap.ssid_len = (uint8_t)strlen((char *)ap.ap.ssid);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", RFLAB_AP_PASS);
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;

    /*
     * 先以 APSTA 配置两个接口(STA-only 时 AP 接口未启用,直接 set_config(AP)
     * 可能返回 ESP_ERR_WIFI_MODE),再切到最终模式。
     * 有凭据:最终 STA-only,AP 关闭;无凭据:AP-only。
     */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    if (have_sta) {
        wifi_config_t sta = {0};
        snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%.*s",
                 (int)sizeof(sta.sta.ssid) - 1, ssid);
        snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%.*s",
                 (int)sizeof(sta.sta.password) - 1, pass);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(have_sta ? WIFI_MODE_STA : WIFI_MODE_AP));

    const esp_timer_create_args_t ctargs = {.callback = connect_timeout_cb, .name = "net_ct"};
    const esp_timer_create_args_t rtargs = {.callback = retry_cb, .name = "net_retry"};
    const esp_timer_create_args_t ftargs = {.callback = fallback_cb, .name = "net_fb"};
    ESP_ERROR_CHECK(esp_timer_create(&ctargs, &s_connect_timer));
    ESP_ERROR_CHECK(esp_timer_create(&rtargs, &s_retry_timer));
    ESP_ERROR_CHECK(esp_timer_create(&ftargs, &s_fallback_timer));

    lock();
    if (have_sta) {
        set_mode_locked(NET_MODE_CONNECTING);
    } else {
        set_mode_locked(NET_MODE_AP);
    }
    unlock();

    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    s_started = true;

    /* AP netif 即使当前关闭也可能已分到 192.168.4.1;记录它供回退显示。 */
    esp_netif_ip_info_t ipi;
    if (esp_netif_get_ip_info(s_ap_netif, &ipi) == ESP_OK && ipi.ip.addr) {
        lock();
        ip4_str(&ipi.ip, s_info.ap_ip);
        refresh_ip_locked();
        unlock();
    }

    if (have_sta) {
        arm_connect_timeout();
        esp_wifi_connect();
    }

    app_mark_state_dirty();
    return ESP_OK;
}

esp_err_t net_set_sta(const char *ssid, const char *pass)
{
    if (!ssid || !pass || strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(pass) > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    save_sta(ssid, pass);

    lock();
    s_info.has_creds = true;
    s_keep_ap_on_success = true;
    snprintf(s_info.sta_ssid, sizeof(s_info.sta_ssid), "%s", ssid);
    s_info.sta_connected = false;
    s_info.sta_ip[0] = 0;
    set_mode_locked(NET_MODE_APSTA);
    unlock();

    if (!s_started) {
        return ESP_OK;
    }

    wifi_config_t sta = {0};
    snprintf((char *)sta.sta.ssid, sizeof(sta.sta.ssid), "%.*s",
             (int)sizeof(sta.sta.ssid) - 1, ssid);
    snprintf((char *)sta.sta.password, sizeof(sta.sta.password), "%.*s",
             (int)sizeof(sta.sta.password) - 1, pass);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    s_retry = 0;
    esp_wifi_disconnect();
    esp_wifi_connect();
    arm_connect_timeout();
    app_mark_state_dirty();
    note("WIFI SET", ssid);
    return ESP_OK;
}

esp_err_t net_clear_sta(void)
{
    erase_sta();

    lock();
    s_info.has_creds = false;
    s_keep_ap_on_success = false;
    s_info.sta_connected = false;
    s_info.sta_ssid[0] = 0;
    s_info.sta_ip[0] = 0;
    s_info.rssi = 0;
    set_mode_locked(NET_MODE_AP);
    unlock();

    if (s_started) {
        stop_timer(s_connect_timer);
        stop_timer(s_retry_timer);
        stop_timer(s_fallback_timer);
        esp_wifi_disconnect();
        esp_wifi_set_mode(WIFI_MODE_AP);
    }
    app_mark_state_dirty();
    note("AP MODE", NULL);
    return ESP_OK;
}

void net_get_info(net_info_t *out)
{
    if (!out) {
        return;
    }
    lock();
    *out = s_info;
    unlock();

    if (out->sta_connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out->rssi = ap.rssi;
        }
    }
}

const char *net_ap_ssid(void)
{
    return s_info.ap_ssid;
}

const char *net_ip(void)
{
    return s_ip;
}
