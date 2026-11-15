/*
 * oled_gfx — 72×40 单色帧缓冲 + 5×7 ASCII 文本。
 *
 * 纯 C,不依赖 ESP-IDF,可在主机上单测(tools/host_test.sh)。
 * 对应 SSD1306 的页式显存:每页 8 行、72 列,一列一个字节,5 页 = 360 字节。
 * 像素 (x,y) 落在 fb[(y/8)*72 + x] 的 bit (y%8);bit0 = 该页最上面一行。
 * 注意:45 是"8×8 瓦片"数(U8g2 的 tile 计数),不是字节数;字节数是 72×5。
 */
#ifndef OLED_GFX_H
#define OLED_GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "font5x7.h"

#define GFX_W 72
#define GFX_H 40
#define GFX_PAGES (GFX_H / 8)          /* 5 页 */
#define GFX_STRIDE GFX_W                /* 72 字节/页,一字节一列 */
#define GFX_FB_BYTES (GFX_PAGES * GFX_STRIDE) /* 360 */
#define GFX_LINE_H 8                    /* 5×7 字形 + 1 行行距 */
#define GFX_COLS (GFX_W / FONT5X7_ADVANCE)    /* 12 字符/行 */

typedef struct {
    uint8_t fb[GFX_FB_BYTES];
} gfx_t;

void gfx_clear(gfx_t *g);
void gfx_pixel(gfx_t *g, int x, int y, bool on);
bool gfx_get_pixel(const gfx_t *g, int x, int y);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool on);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, bool on);
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, bool on);

/* 字符左上角在 (x,y);超出 0x20..0x7E 的字符按 '?' 画。 */
void gfx_char(gfx_t *g, int x, int y, char c);
void gfx_text(gfx_t *g, int x, int y, const char *s);
/* 按 8 px 行高画第 row 行(row=0..4),等价 gfx_text(g, 0, row*8, s)。 */
void gfx_text_row(gfx_t *g, int row, const char *s);

/* 把第 y 行写成 72 个 '#'/'.' + NUL;out 至少 GFX_W+1 字节。 */
void gfx_ascii_line(const gfx_t *g, int y, char *out);

#endif /* OLED_GFX_H */
