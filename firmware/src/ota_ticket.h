/*
 * ota_ticket — one-shot upload token for the OTA endpoint. Pure C, host-tested.
 *
 * Why a ticket instead of HTTP auth: the device has no login (AP password /
 * LAN reachability is the boundary). Flashing firmware is code execution, so
 * the Web endpoint stays closed until a physical serial command (`OTA`) arms
 * it for 10 minutes. This is an application-layer gate, not encryption and
 * not a signature; see docs/01-ui.md §3.4.
 */
#ifndef RFLAB_OTA_TICKET_H
#define RFLAB_OTA_TICKET_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_TOKEN_LEN 8
#define OTA_MAX_FAILS 5

typedef struct {
    bool armed;
    char token[OTA_TOKEN_LEN + 1];
    uint32_t expires_ms;
    uint8_t fails;
} ota_ticket_t;

void ota_ticket_reset(ota_ticket_t *t);

/* Arm with a fresh 8-hex token. Returns false on bad args. */
bool ota_ticket_arm(ota_ticket_t *t, uint32_t now_ms, uint32_t ttl_ms, uint32_t rnd);

/* True while armed and not expired; expires lazily (disarms). */
bool ota_ticket_active(ota_ticket_t *t, uint32_t now_ms);

/* Non-consuming check: used at upload start; wrong tries still count. */
bool ota_ticket_verify(ota_ticket_t *t, uint32_t now_ms, const char *token);

/* One-shot check: a correct token also disarms. */
bool ota_ticket_check(ota_ticket_t *t, uint32_t now_ms, const char *token);

void ota_ticket_disarm(ota_ticket_t *t);

#endif /* RFLAB_OTA_TICKET_H */
