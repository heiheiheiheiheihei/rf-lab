#include "ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "ota_ticket.h"

#define OTA_TTL_MS 600000u      /* 10 minutes */
#define OTA_MIN_IMAGE 0x10000u  /* 64 KiB: reject stubs/typos */
#define OTA_REBOOT_GRACE_MS 1500u

static ota_ticket_t s_ticket;
static const esp_partition_t *s_target;
static esp_ota_handle_t s_handle;
static size_t s_image_len;
static size_t s_written;
static bool s_busy;
static bool s_pending_verify;
static uint32_t s_reboot_at;
static char s_phase[8] = "idle";
static char s_error[72];
static int s_running_slot = -1;
static int s_target_slot = -1;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void set_phase(const char *p)
{
    snprintf(s_phase, sizeof(s_phase), "%s", p);
}

static int slot_of(const esp_partition_t *p)
{
    if (!p || p->type != ESP_PARTITION_TYPE_APP) {
        return -1;
    }
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        return 0;
    }
    if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        return 1;
    }
    return -1;
}

void ota_init(void)
{
    ota_ticket_reset(&s_ticket);
    s_target = NULL;
    s_handle = 0;
    s_image_len = 0;
    s_written = 0;
    s_busy = false;
    s_reboot_at = 0;
    s_error[0] = 0;
    set_phase("idle");

    const esp_partition_t *run = esp_ota_get_running_partition();
    s_running_slot = slot_of(run);
    s_target_slot = -1;

    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending_verify = true;
    }
}

bool ota_arm(char *out, size_t cap)
{
    if (!out || cap < OTA_TOKEN_LEN + 1) {
        return false;
    }
    if (!ota_ticket_arm(&s_ticket, now_ms(), OTA_TTL_MS, esp_random())) {
        return false;
    }
    snprintf(out, cap, "%s", s_ticket.token);
    set_phase("armed");
    s_error[0] = 0;
    return true;
}

void ota_disarm(void)
{
    ota_ticket_disarm(&s_ticket);
    if (!s_busy) {
        set_phase("idle");
    }
}

bool ota_is_armed(void)
{
    return ota_ticket_active(&s_ticket, now_ms());
}

uint32_t ota_remaining_s(void)
{
    if (!ota_is_armed()) {
        return 0;
    }
    int32_t left = (int32_t)(s_ticket.expires_ms - now_ms());
    return left > 0 ? (uint32_t)(left / 1000) : 0;
}

bool ota_check_token(const char *token)
{
    /* Non-consuming: a dropped upload should not burn the ticket. */
    return ota_ticket_verify(&s_ticket, now_ms(), token);
}

esp_err_t ota_begin(size_t image_len)
{
    if (s_busy) {
        snprintf(s_error, sizeof(s_error), "busy");
        return ESP_ERR_INVALID_STATE;
    }
    if (!ota_is_armed()) {
        snprintf(s_error, sizeof(s_error), "not armed");
        return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        snprintf(s_error, sizeof(s_error), "no ota slot");
        return ESP_ERR_NOT_FOUND;
    }
    if (image_len < OTA_MIN_IMAGE || image_len > next->size) {
        snprintf(s_error, sizeof(s_error), "size %u not in [%u,%u]",
                 (unsigned)image_len, (unsigned)OTA_MIN_IMAGE, (unsigned)next->size);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t e = esp_ota_begin(next, image_len, &s_handle);
    if (e != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "begin %s", esp_err_to_name(e));
        return e;
    }
    s_target = next;
    s_target_slot = slot_of(next);
    s_image_len = image_len;
    s_written = 0;
    s_busy = true;
    s_error[0] = 0;
    set_phase("write");
    return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t len)
{
    if (!s_busy || !data || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_written + len > s_image_len) {
        snprintf(s_error, sizeof(s_error), "overflow");
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t e = esp_ota_write(s_handle, data, len);
    if (e != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "write %s", esp_err_to_name(e));
        return e;
    }
    s_written += len;
    return ESP_OK;
}

esp_err_t ota_finish(void)
{
    if (!s_busy || !s_target) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = esp_ota_end(s_handle);
    s_handle = 0;
    s_busy = false;
    if (e != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "image %s", esp_err_to_name(e));
        set_phase("error");
        s_target = NULL;
        return e;
    }
    e = esp_ota_set_boot_partition(s_target);
    if (e != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "select %s", esp_err_to_name(e));
        set_phase("error");
        s_target = NULL;
        return e;
    }
    s_reboot_at = now_ms() + OTA_REBOOT_GRACE_MS;
    ota_ticket_disarm(&s_ticket); /* one successful upload per arm */
    set_phase("done");
    return ESP_OK;
}

void ota_abort(void)
{
    if (s_handle) {
        esp_ota_abort(s_handle);
        s_handle = 0;
    }
    s_busy = false;
    s_target = NULL;
    s_written = 0;
    s_image_len = 0;
    if (strcmp(s_phase, "done") != 0) {
        set_phase("error");
    }
}

int ota_progress(void)
{
    if (s_image_len == 0) {
        return 0;
    }
    return (int)((s_written * 100u) / s_image_len);
}

const char *ota_phase(void)
{
    return s_phase;
}

const char *ota_error(void)
{
    return s_error;
}

int ota_running_slot(void)
{
    return s_running_slot;
}

int ota_target_slot(void)
{
    return s_target_slot;
}

bool ota_busy(void)
{
    return s_busy;
}

bool ota_reboot_due(void)
{
    return strcmp(s_phase, "done") == 0 && s_reboot_at != 0 &&
           (int32_t)(now_ms() - s_reboot_at) >= 0;
}

bool ota_pending_verify(void)
{
    return s_pending_verify;
}

bool ota_confirm_boot(void)
{
    if (!s_pending_verify) {
        return false;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        return false;
    }
    s_pending_verify = false;
    return true;
}
