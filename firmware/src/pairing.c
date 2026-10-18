#include "pairing.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "pairing";

#define NVS_NS      "rflab"
#define KEY_SECRET  "secret"
#define KEY_CLIENT  "client"

static uint8_t  s_secret[PAIRING_SECRET_LEN];
static bool     s_bonded;
static char     s_client[PAIRING_CLIENT_ID_MAX + 1];
/* ⚠️ 仅内存:重启归零。契约 §7.1(1.2) 要求它**持久化**,
 *    否则 App 的 seq 基线会对不齐。阶段 4 实现令牌时必须落 NVS。 */
static uint32_t s_last_seq;

static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static void b64url_encode(const uint8_t *in, size_t in_len, char *out)
{
    size_t i = 0, o = 0;
    while (i + 3 <= in_len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64URL[(v >> 18) & 0x3F];
        out[o++] = B64URL[(v >> 12) & 0x3F];
        out[o++] = B64URL[(v >> 6) & 0x3F];
        out[o++] = B64URL[v & 0x3F];
        i += 3;
    }
    if (in_len - i == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64URL[(v >> 18) & 0x3F];
        out[o++] = B64URL[(v >> 12) & 0x3F];
    } else if (in_len - i == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64URL[(v >> 18) & 0x3F];
        out[o++] = B64URL[(v >> 12) & 0x3F];
        out[o++] = B64URL[(v >> 6) & 0x3F];
    }
    out[o] = '\0';
}

esp_err_t pairing_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        /* 命名空间还不存在 = 从未配对过,这是正常首次启动。 */
        s_bonded = false;
        return ESP_OK;
    }

    size_t len = sizeof(s_secret);
    if (nvs_get_blob(h, KEY_SECRET, s_secret, &len) == ESP_OK && len == PAIRING_SECRET_LEN) {
        s_bonded = true;
        size_t clen = sizeof(s_client) - 1;
        if (nvs_get_str(h, KEY_CLIENT, s_client, &clen) != ESP_OK) {
            s_client[0] = '\0';
        }
    } else {
        s_bonded = false;
    }
    nvs_close(h);

    ESP_LOGI(TAG, "配对状态: %s", s_bonded ? "已绑定" : "未绑定");
    return ESP_OK;
}

bool pairing_is_bonded(void)
{
    return s_bonded;
}

esp_err_t pairing_create(const char *client_id)
{
    if (s_bonded) {
        return ESP_ERR_INVALID_STATE;
    }
    if (client_id == NULL || strlen(client_id) > PAIRING_CLIENT_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 硬件 RNG。契约 §7.1(1.1) 的 rng_failed:这里不降级为弱随机——
     * esp_fill_random 在 ESP32-S3 上由硬件熵源驱动,失败时我们宁可不配对。 */
    esp_fill_random(s_secret, sizeof(s_secret));

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(s_secret); i++) {
        if (s_secret[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        return ESP_FAIL;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY_SECRET, s_secret, sizeof(s_secret));
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_CLIENT, client_id);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }

    strncpy(s_client, client_id, PAIRING_CLIENT_ID_MAX);
    s_client[PAIRING_CLIENT_ID_MAX] = '\0';
    s_bonded = true;
    s_last_seq = 0; /* 契约 §7.1(1.2):重新配对把基线归零 */
    ESP_LOGI(TAG, "首次配对完成,client_id=%s", s_client);
    return ESP_OK;
}

bool pairing_client_matches(const char *client_id)
{
    if (!s_bonded || client_id == NULL) {
        return false;
    }
    return strncmp(s_client, client_id, PAIRING_CLIENT_ID_MAX) == 0;
}

esp_err_t pairing_secret_b64url(char out[PAIRING_SECRET_B64_LEN + 1])
{
    if (!s_bonded) {
        return ESP_ERR_INVALID_STATE;
    }
    b64url_encode(s_secret, sizeof(s_secret), out);
    return ESP_OK;
}

uint32_t pairing_last_seq(void)
{
    return s_last_seq;
}
