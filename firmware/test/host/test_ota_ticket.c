/*
 * ota_ticket.c 的主机单测:token 格式、过期、错误次数、一次性。
 *
 * OTA 的 HTTP 端点、分区写入与回滚在 ESP-IDF 侧;这里只测纯 C 的门票逻辑。
 */
#include <stdio.h>
#include <string.h>

#include "ota_ticket.h"

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_fails++;                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static void test_arm_and_token(void)
{
    ota_ticket_t t;
    ota_ticket_reset(&t);
    CHECK(!t.armed);
    CHECK(strcmp(t.token, "") == 0);

    CHECK(ota_ticket_arm(&t, 1000, 600000, 0x1A2B3C4Du));
    CHECK(t.armed);
    CHECK(strlen(t.token) == OTA_TOKEN_LEN);
    CHECK(strcmp(t.token, "1A2B3C4D") == 0);
    CHECK(ota_ticket_active(&t, 1000));
    CHECK(ota_ticket_active(&t, 1000 + 600000 - 1));

    /* All-zero random is remapped so the token is never 00000000. */
    CHECK(ota_ticket_arm(&t, 0, 1000, 0));
    CHECK(strcmp(t.token, "00000000") != 0);
}

static void test_expiry(void)
{
    ota_ticket_t t;
    ota_ticket_reset(&t);
    CHECK(ota_ticket_arm(&t, 5000, 1000, 0xDEADBEEFu));
    CHECK(ota_ticket_active(&t, 5999));
    CHECK(!ota_ticket_active(&t, 6000)); /* expires at the boundary */
    CHECK(!t.armed);
    CHECK(!ota_ticket_check(&t, 6000, "DEADBEEF"));
}

static void test_check(void)
{
    ota_ticket_t t;
    ota_ticket_reset(&t);
    CHECK(ota_ticket_arm(&t, 0, 10000, 0x00C0FFEEu));
    CHECK(ota_ticket_verify(&t, 1, "00c0ffee")); /* case-insensitive */
    CHECK(t.armed);                              /* verify does not consume */
    CHECK(!ota_ticket_verify(&t, 2, "deadbeef"));
    CHECK(t.armed);
    CHECK(ota_ticket_check(&t, 3, "00C0FFEE"));  /* check consumes */
    CHECK(!t.armed);
    CHECK(!ota_ticket_check(&t, 4, "00C0FFEE"));
}

static void test_wrong_and_lockout(void)
{
    ota_ticket_t t;
    ota_ticket_reset(&t);
    CHECK(ota_ticket_arm(&t, 0, 10000, 0x11223344u));

    for (int i = 0; i < OTA_MAX_FAILS - 1; i++) {
        CHECK(!ota_ticket_check(&t, 10 + (uint32_t)i, "DEADBEEF"));
        CHECK(t.armed);
    }
    CHECK(!ota_ticket_check(&t, 100, "DEADBEEF"));
    CHECK(!t.armed); /* lockout disarms */

    /* A fresh arm clears the failure count. */
    CHECK(ota_ticket_arm(&t, 200, 10000, 0x55667788u));
    CHECK(t.fails == 0);
    CHECK(ota_ticket_check(&t, 201, "55667788"));
}

static void test_disarm(void)
{
    ota_ticket_t t;
    ota_ticket_reset(&t);
    CHECK(ota_ticket_arm(&t, 0, 10000, 0xABCDEF01u));
    ota_ticket_disarm(&t);
    CHECK(!t.armed);
    CHECK(strcmp(t.token, "") == 0);
    CHECK(!ota_ticket_check(&t, 1, "ABCDEF01"));
}

int main(void)
{
    test_arm_and_token();
    test_expiry();
    test_check();
    test_wrong_and_lockout();
    test_disarm();

    printf("%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
