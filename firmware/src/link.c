#include "link.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t seq;
    char line[LINK_LINE_MAX];
} link_slot_t;

static link_slot_t s_ring[LINK_RING_SLOTS];
static volatile uint32_t s_head;

void link_init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    s_head = 0;
}

uint32_t link_head(void)
{
    return __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
}

int link_publish(const char *line)
{
    if (!line) {
        return 0;
    }

    size_t n = strlen(line);
    if (n == 0 || n >= LINK_LINE_MAX) {
        return 0;
    }

    uint32_t seq = link_head() + 1;
    link_slot_t *slot = &s_ring[seq % LINK_RING_SLOTS];

    /* 先让槽位失效,再写内容,最后发布序号;读侧只认 seq 匹配的槽。 */
    __atomic_store_n(&slot->seq, 0, __ATOMIC_RELEASE);
    memcpy(slot->line, line, n + 1);
    __atomic_store_n(&slot->seq, seq, __ATOMIC_RELEASE);
    __atomic_store_n(&s_head, seq, __ATOMIC_RELEASE);
    return 1;
}

int link_publishf(const char *fmt, ...)
{
    char line[LINK_LINE_MAX];
    va_list ap;

    if (!fmt) {
        return 0;
    }
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (n <= 0 || n >= (int)sizeof(line)) {
        return 0;
    }
    return link_publish(line);
}

int link_read(uint32_t *cursor, char *out, size_t cap, uint32_t *seq)
{
    if (!cursor || !out || cap == 0) {
        return 0;
    }

    uint32_t head = link_head();
    if (*cursor >= head) {
        return 0;
    }

    /* 落后超过环容量:最旧的已经被覆盖,跳到最旧仍可读的一条。 */
    if (head - *cursor > LINK_RING_SLOTS) {
        *cursor = head - LINK_RING_SLOTS;
    }

    uint32_t want = *cursor + 1;
    link_slot_t *slot = &s_ring[want % LINK_RING_SLOTS];
    uint32_t got = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);

    *cursor = want;
    if (got != want) {
        return -1; /* 已被覆盖 */
    }

    size_t n = strlen(slot->line);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, slot->line, n);
    out[n] = 0;
    if (seq) {
        *seq = want;
    }
    return 1;
}

size_t link_json_escape(const char *src, char *dst, size_t cap)
{
    size_t o = 0;

    if (!dst || cap == 0) {
        return 0;
    }
    dst[0] = 0;
    if (!src) {
        return 0;
    }

    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        char tmp[8];
        const char *rep = NULL;
        size_t rep_len = 1;

        switch (*p) {
        case '"':
            rep = "\\\"";
            rep_len = 2;
            break;
        case '\\':
            rep = "\\\\";
            rep_len = 2;
            break;
        case '\n':
            rep = "\\n";
            rep_len = 2;
            break;
        case '\r':
            rep = "\\r";
            rep_len = 2;
            break;
        case '\t':
            rep = "\\t";
            rep_len = 2;
            break;
        default:
            if (*p < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
                rep = tmp;
                rep_len = 6;
            }
            break;
        }

        if (rep) {
            if (o + rep_len >= cap) {
                break;
            }
            memcpy(dst + o, rep, rep_len);
            o += rep_len;
        } else {
            if (o + 1 >= cap) {
                break;
            }
            dst[o++] = (char)*p;
        }
    }

    dst[o] = 0;
    return o;
}

size_t link_frame_fill(const char *json, size_t len, size_t chunk, size_t offset,
                       uint8_t *out, size_t cap)
{
    if (!json || !out || cap == 0 || chunk == 0) {
        return 0;
    }

    size_t total = len + 1; /* 数据 + 帧结束符 '\n' */
    if (offset >= total) {
        return 0;
    }

    size_t n = total - offset;
    if (n > chunk) {
        n = chunk;
    }
    if (n > cap) {
        n = cap; /* 调用方 cap 偏小时截断,但 offset 必须按返回值推进 */
    }
    for (size_t i = 0; i < n; i++) {
        size_t at = offset + i;
        out[i] = (at < len) ? (uint8_t)json[at] : (uint8_t)'\n';
    }
    return n;
}
