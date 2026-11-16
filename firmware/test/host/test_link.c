/*
 * link.c 的主机单测:事件环回绕、多消费者 cursor、JSON 转义。
 *
 * 这是 Web/BLE 事件通路的纯 C 部分;不依赖 ESP-IDF,秒级可跑。
 */
#include <stdio.h>
#include <string.h>

#include "link.h"

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

static void test_basic(void)
{
    char out[LINK_LINE_MAX];
    uint32_t seq = 0;
    uint32_t cur = 0;

    link_init();
    CHECK(link_head() == 0);
    CHECK(link_read(&cur, out, sizeof(out), &seq) == 0);

    CHECK(link_publish("{\"t\":\"event\",\"kind\":\"learn\"}") == 1);
    CHECK(link_head() == 1);
    CHECK(link_read(&cur, out, sizeof(out), &seq) == 1);
    CHECK(seq == 1);
    CHECK(strcmp(out, "{\"t\":\"event\",\"kind\":\"learn\"}") == 0);
    CHECK(link_read(&cur, out, sizeof(out), &seq) == 0);

    CHECK(link_publishf("{\"n\":%d}", 42) == 1);
    CHECK(link_read(&cur, out, sizeof(out), &seq) == 1);
    CHECK(seq == 2);
    CHECK(strcmp(out, "{\"n\":42}") == 0);
}

static void test_too_long(void)
{
    char big[LINK_LINE_MAX + 16];
    char out[LINK_LINE_MAX];
    uint32_t seq = 0;
    uint32_t cur = 0;

    memset(big, 'A', sizeof(big));
    big[sizeof(big) - 1] = 0;
    link_init();
    CHECK(link_publish(big) == 0);
    CHECK(link_head() == 0);
    CHECK(link_publishf("%s", big) == 0);
    CHECK(link_head() == 0);
    CHECK(link_read(&cur, out, sizeof(out), &seq) == 0);
}

static void test_wrap_and_cursors(void)
{
    char out[LINK_LINE_MAX];
    uint32_t seq = 0;
    uint32_t slow = 0;
    uint32_t fast = 0;

    link_init();
    for (int i = 0; i < LINK_RING_SLOTS + 5; i++) {
        CHECK(link_publishf("{\"i\":%d}", i) == 1);
    }
    CHECK(link_head() == (uint32_t)(LINK_RING_SLOTS + 5));

    /* 慢消费者落后超过环容量:直接跳到最旧仍可读的一条,不返回脏数据。 */
    CHECK(link_read(&slow, out, sizeof(out), &seq) == 1);
    CHECK(seq == (uint32_t)(LINK_RING_SLOTS + 5 - LINK_RING_SLOTS + 1));
    CHECK(strcmp(out, "{\"i\":5}") == 0);

    /* 快消费者从 0 开始,读到的第一条就是被覆盖标记;之后能追平。 */
    int got = 0;
    for (;;) {
        int r = link_read(&fast, out, sizeof(out), &seq);
        if (r == 0) {
            break;
        }
        if (r == 1) {
            got++;
        }
    }
    CHECK(got > 0);
    CHECK(fast == link_head());
    CHECK(link_read(&fast, out, sizeof(out), &seq) == 0);
}

static void test_escape(void)
{
    char out[256];
    size_t n;

    n = link_json_escape("a\"b\\c\nd\re\tf", out, sizeof(out));
    CHECK(n == strlen(out));
    CHECK(strcmp(out, "a\\\"b\\\\c\\nd\\re\\tf") == 0);

    n = link_json_escape("\x01\x1f", out, sizeof(out));
    CHECK(strcmp(out, "\\u0001\\u001f") == 0);
    CHECK(n == 12);

    /* UTF-8 原样通过(JSON 本身是 UTF-8)。 */
    n = link_json_escape("学习", out, sizeof(out));
    CHECK(strcmp(out, "学习") == 0);
    CHECK(n == 6);

    /* 截断必须仍是 NUL 结尾,且不越界。 */
    n = link_json_escape("abcdef", out, 4);
    CHECK(n == 3);
    CHECK(strcmp(out, "abc") == 0);

    n = link_json_escape("a\"b", out, 3);
    CHECK(n == 1);
    CHECK(strcmp(out, "a") == 0);

    CHECK(link_json_escape(NULL, out, sizeof(out)) == 0);
    CHECK(out[0] == 0);
}

int main(void)
{
    test_basic();
    test_too_long();
    test_wrap_and_cursors();
    test_escape();

    printf("%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
