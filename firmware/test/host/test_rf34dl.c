/*
 * rf34dl.c 的主机单测(gcc,不依赖 ESP-IDF)。
 *
 * 运行:tools/host_test.sh
 * 关键回归:用 2026-11-14 在真模块上抓到的 16 字节(两帧连发)当输入,
 *          证明「文档里的帧格式」与「模块实际输出」是同一件事。
 */
#include <stdio.h>
#include <string.h>

#include "rf34dl.h"

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

#define CHECK_BYTES(a, b, n) CHECK(memcmp((a), (b), (n)) == 0)

static const uint8_t k_real_capture[16] = {
    0xFD, 0xF3, 0x03, 0x55, 0x55, 0x03, 0x3C, 0xDF,
    0xFD, 0xF3, 0x03, 0x55, 0x55, 0x03, 0x4B, 0xDF,
};

/* 喂一段字节,返回事件数;每个帧/应答都记进数组 */
typedef struct {
    int frames;
    int acks;
} count_t;

static count_t feed(rf34dl_parser_t *p, const uint8_t *d, size_t n)
{
    count_t c = {0, 0};

    for (size_t i = 0; i < n; i++) {
        rf34dl_ev_t ev = rf34dl_parser_feed(p, d[i]);
        if (ev == RF34DL_EV_FRAME) {
            c.frames++;
        } else if (ev == RF34DL_EV_ACK) {
            c.acks++;
        }
    }
    return c;
}

static void test_helpers(void)
{
    CHECK(rf34dl_band_ok(RF34DL_BAND_315));
    CHECK(rf34dl_band_ok(RF34DL_BAND_433));
    CHECK(!rf34dl_band_ok(0xF5));
    CHECK(strcmp(rf34dl_band_name(RF34DL_BAND_315), "315M") == 0);
    CHECK(strcmp(rf34dl_band_name(RF34DL_BAND_433), "433M") == 0);

    CHECK(rf34dl_baud_sel(4800) == 1);
    CHECK(rf34dl_baud_sel(9600) == 2);
    CHECK(rf34dl_baud_sel(57600) == 3);
    CHECK(rf34dl_baud_sel(115200) == 4);
    CHECK(rf34dl_baud_sel(19200) == 0);
    CHECK(rf34dl_sel_baud(1) == 4800);
    CHECK(rf34dl_sel_baud(4) == 115200);
    CHECK(rf34dl_sel_baud(0) == 0);
    CHECK(rf34dl_sel_baud(5) == 0);

    CHECK(rf34dl_is_ack(0xF0));
    CHECK(rf34dl_is_ack(0xF3));
    /* 2026-11-15 实测:长发期间 0xFE 心跳(约 25Hz),停止后 0xFF。 */
    CHECK(rf34dl_is_ack(0xFE));
    CHECK(rf34dl_is_ack(0xFF));
    CHECK(!rf34dl_is_ack(0xF4));
    CHECK(!rf34dl_is_ack(0xEF));
}

static void test_build(void)
{
    uint8_t f[8];
    const uint8_t code[4] = {0x01, 0x02, 0x03, 0x60};
    const uint8_t want315[8] = {0xFD, 0xF3, 0x03, 0x01, 0x02, 0x03, 0x60, 0xDF};
    const uint8_t want433[8] = {0xFD, 0xF4, 0x03, 0x01, 0x02, 0x03, 0x60, 0xDF};
    const uint8_t want_long[8] = {0xFD, 0xF3, 0xFE, 0x01, 0x02, 0x03, 0x60, 0xDF};
    const uint8_t want_stop[8] = {0xFD, 0xF3, 0xFF, 0x01, 0x02, 0x03, 0x60, 0xDF};

    /* 与模块说明书 §3.1 的两条示例逐字节一致 */
    CHECK(rf34dl_build_tx(f, RF34DL_BAND_315, RF34DL_DUR_NORMAL, code) == 8);
    CHECK_BYTES(f, want315, 8);

    CHECK(rf34dl_build_tx(f, RF34DL_BAND_433, RF34DL_DUR_NORMAL, code) == 8);
    CHECK_BYTES(f, want433, 8);

    CHECK(rf34dl_build_tx(f, RF34DL_BAND_315, RF34DL_DUR_LONG, code) == 8);
    CHECK_BYTES(f, want_long, 8);

    CHECK(rf34dl_build_tx(f, RF34DL_BAND_315, RF34DL_DUR_STOP, code) == 8);
    CHECK_BYTES(f, want_stop, 8);

    /* 非法频段:返回 0,且不写缓冲 */
    memset(f, 0xAA, sizeof(f));
    CHECK(rf34dl_build_tx(f, 0xF5, RF34DL_DUR_NORMAL, code) == 0);
    CHECK(f[0] == 0xAA && f[7] == 0xAA);

    /* 波特率:01=4800 02=9600 03=57600 04=115200 */
    const uint8_t want_b[4][8] = {
        {0xFD, 0x01, 0xF1, 0, 0, 0, 0, 0xDF},
        {0xFD, 0x02, 0xF1, 0, 0, 0, 0, 0xDF},
        {0xFD, 0x03, 0xF1, 0, 0, 0, 0, 0xDF},
        {0xFD, 0x04, 0xF1, 0, 0, 0, 0, 0xDF},
    };
    for (int sel = 1; sel <= 4; sel++) {
        CHECK(rf34dl_build_baud(f, sel) == 8);
        CHECK_BYTES(f, want_b[sel - 1], 8);
    }
    CHECK(rf34dl_build_baud(f, 0) == 0);
    CHECK(rf34dl_build_baud(f, 5) == 0);

    /* 输出模式:single=FD 01 F2.., continuous=FD 02 F2.. */
    const uint8_t want_single[8] = {0xFD, 0x01, 0xF2, 0, 0, 0, 0, 0xDF};
    const uint8_t want_cont[8] = {0xFD, 0x02, 0xF2, 0, 0, 0, 0, 0xDF};

    CHECK(rf34dl_build_mode(f, false) == 8);
    CHECK_BYTES(f, want_single, 8);
    CHECK(rf34dl_build_mode(f, true) == 8);
    CHECK_BYTES(f, want_cont, 8);

    /* 组帧 → 解帧 round-trip */
    rf34dl_parser_t p;
    rf34dl_parser_init(&p);
    const uint8_t code2[4] = {0x55, 0x55, 0x03, 0x3C};
    rf34dl_build_tx(f, RF34DL_BAND_433, RF34DL_DUR_NORMAL, code2);
    count_t c = feed(&p, f, 8);
    CHECK(c.frames == 1);
    CHECK(p.frame.band == RF34DL_BAND_433);
    CHECK(p.frame.b3 == RF34DL_DUR_NORMAL);
    CHECK_BYTES(p.frame.code, code2, 4);
}

static void test_parser_real_capture(void)
{
    rf34dl_parser_t p;

    rf34dl_parser_init(&p);
    count_t c = feed(&p, k_real_capture, 16);
    CHECK(c.frames == 2);
    CHECK(c.acks == 0);
    CHECK(p.stray == 0);
    CHECK(p.dropped == 0);
    CHECK(p.frame.band == RF34DL_BAND_315);
    CHECK(p.frame.b3 == RF34DL_RX_FIXED);

    /* 第二帧的码:55 55 03 4B —— 同一遥控器的另一个键 */
    const uint8_t want2[4] = {0x55, 0x55, 0x03, 0x4B};
    CHECK_BYTES(p.frame.code, want2, 4);
}

static void test_parser_resync(void)
{
    rf34dl_parser_t p;

    /* 帧前有垃圾:垃圾计入 stray,帧照样出来 */
    rf34dl_parser_init(&p);
    const uint8_t junk[] = {0x00, 0x11, 0x22};
    count_t c = feed(&p, junk, sizeof(junk));
    CHECK(c.frames == 0);
    CHECK(p.stray == 3);
    c = feed(&p, k_real_capture, 8);
    CHECK(c.frames == 1);

    /* 截断帧:只有 5 字节就被下一帧的 FD 打断 → 重同步后只认后一帧 */
    rf34dl_parser_init(&p);
    const uint8_t trunc_then_frame[] = {
        0xFD, 0xF3, 0x03, 0x11, 0x22, /* 截断 */
        0xFD, 0xF4, 0x03, 0x33, 0x44, 0x55, 0x66, 0xDF,
    };
    c = feed(&p, trunc_then_frame, sizeof(trunc_then_frame));
    CHECK(c.frames == 1);
    CHECK(p.frame.band == RF34DL_BAND_433);
    CHECK(p.dropped >= 1);

    /* 频段非法:不认 */
    rf34dl_parser_init(&p);
    const uint8_t bad_band[8] = {0xFD, 0xF5, 0x03, 0, 0, 0, 0, 0xDF};
    c = feed(&p, bad_band, 8);
    CHECK(c.frames == 0);

    /* 帧内出现 FD/DF 也不影响(码可以是任意字节) */
    rf34dl_parser_init(&p);
    const uint8_t fd_code[8] = {0xFD, 0xF3, 0x03, 0xFD, 0xDF, 0xFD, 0xDF, 0xDF};
    c = feed(&p, fd_code, 8);
    CHECK(c.frames == 1);
    const uint8_t want_fd[4] = {0xFD, 0xDF, 0xFD, 0xDF};
    CHECK_BYTES(p.frame.code, want_fd, 4);
}

static void test_parser_acks(void)
{
    rf34dl_parser_t p;

    rf34dl_parser_init(&p);
    const uint8_t acks[] = {0xF0, 0xF1, 0xF2, 0xF3};
    count_t c = feed(&p, acks, sizeof(acks));
    CHECK(c.acks == 4);
    CHECK(p.ack == 0xF3);
    CHECK(c.frames == 0);

    /* F4 是频段字节,不是应答;单独出现应算 stray */
    rf34dl_parser_init(&p);
    const uint8_t not_ack[] = {0xF4, 0xF5, 0x00};
    c = feed(&p, not_ack, sizeof(not_ack));
    CHECK(c.acks == 0);
    CHECK(p.stray == 3);

    /* 长发心跳:FE 连续出现,停止后 FF;全部是状态,不应计成 stray */
    rf34dl_parser_init(&p);
    const uint8_t long_stop[] = {0xFE, 0xFE, 0xFE, 0xFF};
    c = feed(&p, long_stop, sizeof(long_stop));
    CHECK(c.acks == 4);
    CHECK(c.frames == 0);
    CHECK(p.ack == 0xFF);
    CHECK(p.stray == 0);
    CHECK(p.dropped == 0);
}

static void test_fuzz(void)
{
    rf34dl_parser_t p;
    uint32_t x = 12345;

    rf34dl_parser_init(&p);
    for (int i = 0; i < 200000; i++) {
        x = x * 1103515245u + 12345u;
        rf34dl_parser_feed(&p, (uint8_t)(x >> 16));
    }
    /* 不崩、状态不越界就是通过 */
    CHECK(p.st.len < RF34DL_FRAME_LEN);
    CHECK(p.stray + p.dropped > 0);
}

int main(void)
{
    test_helpers();
    test_build();
    test_parser_real_capture();
    test_parser_resync();
    test_parser_acks();
    test_fuzz();

    printf("%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
