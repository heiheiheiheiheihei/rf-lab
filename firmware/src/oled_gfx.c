/*
 * oled_gfx — 72×40 单色帧缓冲实现。纯 C,主机可单测。
 */
#include "oled_gfx.h"

#include <string.h>

void gfx_clear(gfx_t *g)
{
    memset(g->fb, 0, sizeof(g->fb));
}

void gfx_pixel(gfx_t *g, int x, int y, bool on)
{
    if (!g || x < 0 || x >= GFX_W || y < 0 || y >= GFX_H) {
        return;
    }

    uint8_t *b = &g->fb[(y / 8) * GFX_STRIDE + x];
    uint8_t bit = (uint8_t)(1u << (y % 8));

    if (on) {
        *b |= bit;
    } else {
        *b &= (uint8_t)~bit;
    }
}

bool gfx_get_pixel(const gfx_t *g, int x, int y)
{
    if (!g || x < 0 || x >= GFX_W || y < 0 || y >= GFX_H) {
        return false;
    }
    return (g->fb[(y / 8) * GFX_STRIDE + x] >> (y % 8)) & 1u;
}

/* 任意方向直线,用于测试图案的对角线(Bresenham)。 */
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, bool on)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        gfx_pixel(g, x0, y0, on);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_rect(gfx_t *g, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_line(g, x, y, x + w - 1, y, on);
    gfx_line(g, x, y + h - 1, x + w - 1, y + h - 1, on);
    gfx_line(g, x, y, x, y + h - 1, on);
    gfx_line(g, x + w - 1, y, x + w - 1, y + h - 1, on);
}

void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            gfx_pixel(g, x + i, y + j, on);
        }
    }
}

void gfx_char(gfx_t *g, int x, int y, char c)
{
    int idx = (unsigned char)c - FONT5X7_FIRST;

    if (idx < 0 || idx > (FONT5X7_LAST - FONT5X7_FIRST)) {
        idx = '?' - FONT5X7_FIRST;
    }
    for (int col = 0; col < FONT5X7_W; col++) {
        uint8_t bits = k_font5x7[idx][col];
        for (int row = 0; row < FONT5X7_H; row++) {
            if (bits & (1u << row)) {
                gfx_pixel(g, x + col, y + row, true);
            }
        }
    }
}

void gfx_text(gfx_t *g, int x, int y, const char *s)
{
    if (!s) {
        return;
    }
    for (; *s; s++, x += FONT5X7_ADVANCE) {
        if (x >= GFX_W) {
            break;
        }
        gfx_char(g, x, y, *s);
    }
}

void gfx_text_row(gfx_t *g, int row, const char *s)
{
    if (row < 0 || row >= GFX_PAGES) {
        return;
    }
    gfx_text(g, 0, row * GFX_LINE_H, s);
}

void gfx_ascii_line(const gfx_t *g, int y, char *out)
{
    if (!out) {
        return;
    }
    if (!g || y < 0 || y >= GFX_H) {
        out[0] = 0;
        return;
    }
    for (int x = 0; x < GFX_W; x++) {
        out[x] = gfx_get_pixel(g, x, y) ? '#' : '.';
    }
    out[GFX_W] = 0;
}
