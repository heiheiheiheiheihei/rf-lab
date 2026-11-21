/*
 * ota — dual-slot firmware update for the ESP32-C3 (ESP-IDF side).
 *
 * Transport-agnostic: web_server streams bytes into ota_begin/ota_write/
 * ota_finish, main polls progress/reboot. The one-shot ticket lives in
 * ota_ticket.c (pure C, host-tested).
 *
 * Safety: writes go to the inactive slot; a failed/aborted upload leaves the
 * running image untouched. New images boot as PENDING_VERIFY and are marked
 * valid by main after a short health window; before that the bootloader can
 * roll back.
 */
#ifndef RFLAB_OTA_H
#define RFLAB_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Read running slot / pending-verify state. Call once at boot. */
void ota_init(void);

/* Serial-only arm. Copies the 8-hex token into out. */
bool ota_arm(char *out, size_t cap);
void ota_disarm(void);
bool ota_is_armed(void);
uint32_t ota_remaining_s(void);

/* One-shot token check (locks out after OTA_MAX_FAILS). */
bool ota_check_token(const char *token);

/* Upload stream. begin -> write* -> finish (or abort). */
esp_err_t ota_begin(size_t image_len);
esp_err_t ota_write(const void *data, size_t len);
esp_err_t ota_finish(void);
void ota_abort(void);

/* Status for /api/state and serial logging. */
int ota_progress(void);          /* 0..100, 0 when idle */
const char *ota_phase(void);     /* idle|armed|write|verify|done|error */
const char *ota_error(void);     /* last error text, "" when none */
int ota_running_slot(void);      /* 0 = ota_0, 1 = ota_1 */
int ota_target_slot(void);       /* -1 when no target */
bool ota_busy(void);
bool ota_reboot_due(void);       /* done + 1.5s grace elapsed */

/* PENDING_VERIFY bookkeeping. */
bool ota_pending_verify(void);
bool ota_confirm_boot(void);

#endif /* RFLAB_OTA_H */
