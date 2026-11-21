#include "ota_ticket.h"

#include <stdio.h>
#include <string.h>

static bool expired(uint32_t now_ms, uint32_t expires_ms)
{
    /* Signed difference: safe across the 49-day millisecond wrap. */
    return (int32_t)(now_ms - expires_ms) >= 0;
}

void ota_ticket_reset(ota_ticket_t *t)
{
    if (!t) {
        return;
    }
    t->armed = false;
    t->token[0] = 0;
    t->expires_ms = 0;
    t->fails = 0;
}

bool ota_ticket_arm(ota_ticket_t *t, uint32_t now_ms, uint32_t ttl_ms, uint32_t rnd)
{
    if (!t || ttl_ms == 0) {
        return false;
    }
    uint32_t v = rnd ? rnd : 0xA5C3E1F0u; /* never emit 00000000 */
    snprintf(t->token, sizeof(t->token), "%08X", (unsigned)v);
    t->armed = true;
    t->expires_ms = now_ms + ttl_ms;
    t->fails = 0;
    return true;
}

void ota_ticket_disarm(ota_ticket_t *t)
{
    if (!t) {
        return;
    }
    t->armed = false;
    t->token[0] = 0;
    t->fails = 0;
}

bool ota_ticket_active(ota_ticket_t *t, uint32_t now_ms)
{
    if (!t || !t->armed) {
        return false;
    }
    if (expired(now_ms, t->expires_ms)) {
        ota_ticket_disarm(t);
        return false;
    }
    return true;
}

static char upper_hex(char c)
{
    if (c >= 'a' && c <= 'f') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

bool ota_ticket_verify(ota_ticket_t *t, uint32_t now_ms, const char *token)
{
    if (!ota_ticket_active(t, now_ms)) {
        return false;
    }
    if (!token) {
        return false;
    }
    size_t n = strlen(token);
    if (n != OTA_TOKEN_LEN) {
        if (++t->fails >= OTA_MAX_FAILS) {
            ota_ticket_disarm(t);
        }
        return false;
    }
    bool same = true;
    for (size_t i = 0; i < OTA_TOKEN_LEN; i++) {
        if (upper_hex(token[i]) != t->token[i]) {
            same = false;
        }
    }
    if (!same) {
        if (++t->fails >= OTA_MAX_FAILS) {
            ota_ticket_disarm(t);
        }
        return false;
    }
    return true;
}

bool ota_ticket_check(ota_ticket_t *t, uint32_t now_ms, const char *token)
{
    if (!ota_ticket_verify(t, now_ms, token)) {
        return false;
    }
    ota_ticket_disarm(t);
    return true;
}
