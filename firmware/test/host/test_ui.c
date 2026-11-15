/*
 * oled_gfx.c / ui.c / oled_panel.h 的主机单测(gcc,不依赖 ESP-IDF)。
 *
 * 运行:tools/host_test.sh
 * 重点:72×40 帧缓冲的位映射、5×7 字库、12×5 状态页文本、SSD1306 几何常量。
 *       I2C 传输本身不能主机测,只能上机;这里钉住的是"写什么、写到哪里"。
 */
#include <stdio.h>
#include <string.h>

#include "oled_gfx.h"
#include "oled_panel.h"
#include "ui.h"

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

static void check_row(char rows[UI_ROWS][UI_COLS + 1], int r, const char *want)
{
    g_checks++;
    if (strcmp(rows[r], want) != 0) {
        g_fails++;
        printf("FAIL %s:%d row %d\n  want \"%s\" (%zu)\n  got  \"%s\" (%zu)\n", __FILE__,
               __LINE__, r, want, strlen(want), rows[r], strlen(rows[r]));
    }
}

static const uint8_t k_code_a[4] = {0x55, 0x55, 0x03, 0x4C};
static const uint8_t k_code_b[4] = {0x55, 0x55, 0x03, 0x4B};

static void test_panel_profile(void)
{
    /* 几何来自 U8g2 72x40 ER + Zephyr 01Space DTS,见 oled_panel.h。 */
    CHECK(OLED_PANEL_W == 72);
    CHECK(OLED_PANEL_H == 40);
    CHECK(OLED_PANEL_PAGES == 5);
    CHECK(OLED_PANEL_COL_OFFSET == 28);
    CHECK(OLED_PANEL_COL_END == 99);
    CHECK(OLED_PANEL_ADDR == 0x3C);
    CHECK(GFX_W == OLED_PANEL_W);
    CHECK(GFX_H == OLED_PANEL_H);
    CHECK(GFX_PAGES == OLED_PANEL_PAGES);
    CHECK(GFX_FB_BYTES == 360);

    /* 关键初始化参数必须留在序列里,且顺序不能被随手打乱。 */
    CHECK(sizeof(k_oled_panel_init) == 27);
    const uint8_t must_have[][2] = {
        {0xA8, 0x27}, /* multiplex 1/40 */
        {0xD3, 0x00}, /* display offset 0 */
        {0xAD, 0x30}, /* internal IREF(72×40 专有) */
        {0x8D, 0x14}, /* charge pump on */
        {0x20, 0x00}, /* horizontal addressing */
        {0xA1, 0x00}, /* segment remap;0x00 用不到,只占位 */
        {0xDA, 0x12}, /* COM pins */
        {0xD9, 0x22},
        {0xDB, 0x20},
    };
    /* 0xA1/0xC8 是单字节命令,单独查。 */
    bool a1 = false, c8 = false;
    for (size_t i = 0; i < sizeof(k_oled_panel_init); i++) {
        if (k_oled_panel_init[i] == 0xA1) a1 = true;
        if (k_oled_panel_init[i] == 0xC8) c8 = true;
    }
    CHECK(a1);
    CHECK(c8);
    for (size_t i = 0; i < sizeof(must_have) / sizeof(must_have[0]); i++) {
        if (must_have[i][1] == 0x00 && must_have[i][0] == 0xA1) {
            continue; /* 占位项 */
        }
        bool found = false;
        for (size_t j = 0; j + 1 < sizeof(k_oled_panel_init); j++) {
            if (k_oled_panel_init[j] == must_have[i][0] &&
                k_oled_panel_init[j + 1] == must_have[i][1]) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}

static void test_pixels_and_bounds(void)
{
    gfx_t g;

    gfx_clear(&g);
    for (size_t i = 0; i < GFX_FB_BYTES; i++) {
        CHECK(g.fb[i] == 0);
    }

    /* 越界写必须被忽略,不能踩到缓冲区外(ASan 会抓)。 */
    gfx_pixel(&g, -1, 0, true);
    gfx_pixel(&g, GFX_W, 0, true);
    gfx_pixel(&g, 0, -1, true);
    gfx_pixel(&g, 0, GFX_H, true);
    CHECK(!gfx_get_pixel(&g, -1, 0));
    CHECK(!gfx_get_pixel(&g, GFX_W, 0));
    for (size_t i = 0; i < GFX_FB_BYTES; i++) {
        CHECK(g.fb[i] == 0);
    }

    /* 页式映射:(71,39) -> 第 4 页第 8 字节 bit7。 */
    gfx_pixel(&g, 71, 39, true);
    CHECK(g.fb[4 * GFX_STRIDE + 71] == 0x80);
    CHECK(gfx_get_pixel(&g, 71, 39));
    CHECK(!gfx_get_pixel(&g, 71, 38));
    gfx_pixel(&g, 71, 39, false);
    CHECK(g.fb[4 * GFX_STRIDE + 71] == 0x00);

    gfx_pixel(&g, 0, 0, true);
    CHECK(g.fb[0] == 0x01);
    gfx_pixel(&g, 0, 0, false);
    CHECK(g.fb[0] == 0x00);
}

static void test_glyphs(void)
{
    gfx_t g;

    gfx_clear(&g);
    gfx_char(&g, 0, 0, 'A');
    /* 0x41 'A' = 7C 12 11 12 7C,bit0 是顶行。 */
    CHECK(gfx_get_pixel(&g, 2, 0));
    CHECK(gfx_get_pixel(&g, 0, 2));
    CHECK(gfx_get_pixel(&g, 2, 4));
    CHECK(gfx_get_pixel(&g, 4, 6));
    CHECK(!gfx_get_pixel(&g, 0, 0));
    CHECK(!gfx_get_pixel(&g, 0, 1));

    /* 不可打印字符按 '?' 画,不能读越界。 */
    gfx_clear(&g);
    gfx_char(&g, 0, 0, (char)0x01);
    CHECK(gfx_get_pixel(&g, 0, 0) || gfx_get_pixel(&g, 1, 0) || gfx_get_pixel(&g, 2, 0));

    /* 文本步进 6 px;右侧越界字符被裁掉。 */
    gfx_clear(&g);
    gfx_text(&g, 66, 36, "AB");
    CHECK(gfx_get_pixel(&g, 68, 36)); /* 'A' 的顶点 */
    CHECK(!gfx_get_pixel(&g, 66, 36));
    /* 13 个字符 = 78 px,最后一个必须整字被裁掉而不越界。 */
    gfx_text(&g, 0, 0, "1234567890123");
    CHECK(gfx_get_pixel(&g, 66, 0) || gfx_get_pixel(&g, 67, 0));

    gfx_text(&g, 0, 0, NULL); /* 不崩 */
}

static void test_lines_and_box(void)
{
    gfx_t g;

    gfx_clear(&g);
    gfx_rect(&g, 0, 0, GFX_W, GFX_H, true);
    for (int x = 0; x < GFX_W; x++) {
        CHECK(gfx_get_pixel(&g, x, 0));
        CHECK(gfx_get_pixel(&g, x, GFX_H - 1));
    }
    for (int y = 0; y < GFX_H; y++) {
        CHECK(gfx_get_pixel(&g, 0, y));
        CHECK(gfx_get_pixel(&g, GFX_W - 1, y));
    }
    CHECK(!gfx_get_pixel(&g, 1, 1));

    gfx_clear(&g);
    gfx_fill_rect(&g, 10, 10, 3, 4, true);
    for (int y = 10; y < 14; y++) {
        for (int x = 10; x < 13; x++) {
            CHECK(gfx_get_pixel(&g, x, y));
        }
    }
    CHECK(!gfx_get_pixel(&g, 13, 10));
    CHECK(!gfx_get_pixel(&g, 10, 14));

    /* 对角线必须从左上到右下(U8g2 的 A1/C8 组合),用来验收方向。 */
    gfx_clear(&g);
    gfx_line(&g, 0, 0, GFX_W - 1, GFX_H - 1, true);
    CHECK(gfx_get_pixel(&g, 0, 0));
    CHECK(gfx_get_pixel(&g, GFX_W - 1, GFX_H - 1));
    CHECK(!gfx_get_pixel(&g, GFX_W - 1, 0));
    CHECK(!gfx_get_pixel(&g, 0, GFX_H - 1));
}

static void test_ascii_dump(void)
{
    gfx_t g;
    char line[GFX_W + 1];

    gfx_clear(&g);
    gfx_pixel(&g, 0, 0, true);
    gfx_pixel(&g, GFX_W - 1, 0, true);
    gfx_ascii_line(&g, 0, line);
    CHECK(strlen(line) == GFX_W);
    CHECK(line[0] == '#');
    CHECK(line[GFX_W - 1] == '#');
    CHECK(line[1] == '.');

    gfx_ascii_line(&g, -1, line);
    CHECK(line[0] == 0);
    gfx_ascii_line(&g, GFX_H, line);
    CHECK(line[0] == 0);
}

static void test_ui_idle(void)
{
    ui_model_t m;
    char rows[UI_ROWS][UI_COLS + 1];

    ui_init(&m, "RF-Lab 0.5.0", 0, 32, true);
    ui_note_counters(&m, 0, 0, 0);
    ui_render(&m, rows);
    check_row(rows, 0, "RF-Lab 0.5.0");
    check_row(rows, 1, "READY 0/32  ");
    check_row(rows, 2, "no code yet ");
    check_row(rows, 3, "press remote");
    check_row(rows, 4, "F0 A0 S0    ");

    ui_init(&m, "RF-Lab 0.5.0", 3, 32, true);
    ui_note_seed(&m, 3, RF34DL_BAND_315, k_code_b);
    ui_note_counters(&m, 17, 2, 0);
    ui_render(&m, rows);
    check_row(rows, 1, "READY 3/32  ");
    check_row(rows, 2, "5555034B    ");
    check_row(rows, 3, "315M id=3   ");
    check_row(rows, 4, "F17 A2 S0   ");

    ui_note_forget(&m);
    ui_render(&m, rows);
    check_row(rows, 2, "no code yet ");
    check_row(rows, 3, "press remote");
}

static void test_ui_learn_rx(void)
{
    ui_model_t m;
    char rows[UI_ROWS][UI_COLS + 1];

    ui_init(&m, "RF-Lab 0.5.0", 3, 32, true);
    ui_note_count(&m, 4);
    ui_note_learn(&m, 4, RF34DL_BAND_315, k_code_a);
    ui_render(&m, rows);
    check_row(rows, 0, "LEARN ID 4  ");
    check_row(rows, 1, "315M        ");
    check_row(rows, 2, "5555034C    ");
    check_row(rows, 3, "saved 4/32  ");
    check_row(rows, 4, "F0 A0 S0    ");

    ui_note_rx(&m, 2, RF34DL_BAND_433, k_code_b);
    ui_render(&m, rows);
    check_row(rows, 0, "RX ID 2     ");
    check_row(rows, 1, "433M        ");
    check_row(rows, 3, "known       ");

    ui_note_full(&m, RF34DL_BAND_315, k_code_a);
    ui_render(&m, rows);
    check_row(rows, 0, "STORE FULL  ");
    check_row(rows, 3, "not saved   ");
}

static void test_ui_tx(void)
{
    ui_model_t m;
    char rows[UI_ROWS][UI_COLS + 1];

    ui_init(&m, "RF-Lab 0.5.0", 3, 32, true);
    ui_note_tx(&m, UI_TX_SEND, 1, RF34DL_BAND_315, k_code_a);
    ui_render(&m, rows);
    check_row(rows, 0, "TX ID 1     ");
    check_row(rows, 3, "sending...  ");

    ui_note_tx_result(&m, true, 852);
    ui_render(&m, rows);
    check_row(rows, 0, "TX ID 1 OK  ");
    check_row(rows, 3, "F0 852ms    ");

    ui_note_tx(&m, UI_TX_SEND, 1, RF34DL_BAND_315, k_code_a);
    ui_note_tx_result(&m, false, 1500);
    ui_render(&m, rows);
    check_row(rows, 0, "TX ID 1 WARN");
    check_row(rows, 3, "no F0 1500ms");

    /* 长发/停止:结果回调不应把页面改成 WARN(模块本来就不回 F0)。 */
    ui_note_tx(&m, UI_TX_LONG, 1, RF34DL_BAND_315, k_code_a);
    ui_note_tx_result(&m, false, 1500);
    ui_render(&m, rows);
    check_row(rows, 0, "LONG ID 1   ");
    check_row(rows, 3, "until STOP  ");

    ui_note_tx(&m, UI_TX_STOP, 1, RF34DL_BAND_315, k_code_a);
    ui_render(&m, rows);
    check_row(rows, 0, "STOP ID 1   ");
    check_row(rows, 3, "stopping    ");
}

static void test_ui_error(void)
{
    ui_model_t m;
    char rows[UI_ROWS][UI_COLS + 1];

    ui_init(&m, "RF-Lab 0.5.0", 3, 32, true);
    ui_note_error(&m, "NVS FAIL");
    ui_render(&m, rows);
    check_row(rows, 0, "ERROR       ");
    check_row(rows, 1, "NVS FAIL    ");
    check_row(rows, 2, "codes 3     ");

    /* 长错误信息必须截断到 12 字符,不能溢出。 */
    ui_note_error(&m, "a very long error message");
    ui_render(&m, rows);
    check_row(rows, 1, "a very long ");
    CHECK(strlen(rows[1]) == UI_COLS);
}

static void test_ui_padding_and_boot(void)
{
    ui_model_t m;
    char rows[UI_ROWS][UI_COLS + 1];

    ui_init(&m, "RF-Lab 0.5.0", 3, 32, true);
    ui_note_boot(&m, "OLED 72x40", "I2C 3C OK");
    ui_render(&m, rows);
    check_row(rows, 0, "RF-Lab 0.5.0");
    check_row(rows, 1, "OLED 72x40  ");
    check_row(rows, 2, "I2C 3C OK   ");
    check_row(rows, 3, "codes 3     ");

    for (int r = 0; r < UI_ROWS; r++) {
        CHECK(strlen(rows[r]) == UI_COLS);
    }

    /* 标题超长也按 12 字节截断。 */
    ui_init(&m, "0123456789ABCDEF", 0, 32, false);
    ui_note_idle(&m);
    ui_render(&m, rows);
    check_row(rows, 0, "0123456789AB");
    CHECK(!m.present);
}

static void test_test_pattern(void)
{
    gfx_t g;

    ui_test_pattern(&g);

    for (int x = 0; x < GFX_W; x++) {
        CHECK(gfx_get_pixel(&g, x, 0));
        CHECK(gfx_get_pixel(&g, x, GFX_H - 1));
    }
    for (int y = 0; y < GFX_H; y++) {
        CHECK(gfx_get_pixel(&g, 0, y));
        CHECK(gfx_get_pixel(&g, GFX_W - 1, y));
    }

    /* 非对称角块:左上大、右上小、左下小、右下大。 */
    CHECK(gfx_get_pixel(&g, 1, 1));
    CHECK(gfx_get_pixel(&g, 4, 4));
    CHECK(gfx_get_pixel(&g, GFX_W - 2, 1));
    CHECK(gfx_get_pixel(&g, 1, GFX_H - 2));
    CHECK(gfx_get_pixel(&g, GFX_W - 2, GFX_H - 2));
    CHECK(gfx_get_pixel(&g, GFX_W - 3, GFX_H - 3));

    /* 文字区必须有像素(不能只剩边框)。 */
    int lit = 0;
    for (int y = 2; y < 36; y++) {
        for (int x = 2; x < 30; x++) {
            if (gfx_get_pixel(&g, x, y)) {
                lit++;
            }
        }
    }
    CHECK(lit > 30);
}

int main(void)
{
    test_panel_profile();
    test_pixels_and_bounds();
    test_glyphs();
    test_lines_and_box();
    test_ascii_dump();
    test_ui_idle();
    test_ui_learn_rx();
    test_ui_tx();
    test_ui_error();
    test_ui_padding_and_boot();
    test_test_pattern();

    printf("%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
