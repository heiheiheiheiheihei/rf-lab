/*
 * oled_panel — 本板 0.42" 72×40 SSD1306 的几何与初始化参数(单一数据源)。
 *
 * 参数来源(2026-11-15 核对):
 *   · U8g2 `u8x8_d_ssd1306_72x40.c`(EastRising 0.42" 72×40,SSD1306 兼容):
 *     初始化序列、MUX 0x27、COM 0x12、内部 IREF 0xAD 0x30、列偏移 28。
 *   · Zephyr `boards/01space/esp32c3_042_oled/esp32c3_042_oled.dts`:
 *     segment-offset = 28,page-offset = 0,display-offset = 0,width 72,height 40。
 *
 * 显存是 128×64,但本屏只映射 72×40:列 28..99,页 0..4。
 * 这几条被主机单测钉住(test_ui.c),改屏/改批次先改这里并重跑上机图案。
 */
#ifndef OLED_PANEL_H_INCLUDED
#define OLED_PANEL_H_INCLUDED

#include <stdint.h>

#define OLED_PANEL_W 72
#define OLED_PANEL_H 40
#define OLED_PANEL_PAGES (OLED_PANEL_H / 8)     /* 5 */
#define OLED_PANEL_COL_OFFSET 28                /* 0x1C */
#define OLED_PANEL_COL_END (OLED_PANEL_COL_OFFSET + OLED_PANEL_W - 1) /* 99 = 0x63 */
#define OLED_PANEL_ADDR 0x3C

/*
 * 初始化序列(不含最后的 Display ON;驱动在延时后单独发,方便统计/排障)。
 * 逐条顺序与 U8g2 一致,不要随手重排 —— 0xAD/0x30、0xDA/0x12 是 72×40 专有参数。
 */
static const uint8_t k_oled_panel_init[] = {
    0xAE,       /* display off */
    0xD5, 0x80, /* clock divide / oscillator */
    0xA8, 0x27, /* multiplex ratio: 1/40 duty */
    0xD3, 0x00, /* display offset: 0 */
    0xAD, 0x30, /* internal IREF (72×40 专有) */
    0x8D, 0x14, /* charge pump on */
    0x40,       /* start line 0 */
    0xA6,       /* normal (non-inverted) */
    0xA4,       /* resume RAM content */
    0x20, 0x00, /* horizontal addressing mode */
    0xA1,       /* segment remap: column 127 -> 0 */
    0xC8,       /* COM scan direction: reverse */
    0xDA, 0x12, /* COM pins: alternative config, no L/R remap */
    0x81, 0xAF, /* contrast */
    0xD9, 0x22, /* pre-charge period */
    0xDB, 0x20, /* VCOMH deselect */
    0x2E,       /* deactivate scroll */
};

#endif /* OLED_PANEL_H_INCLUDED */
