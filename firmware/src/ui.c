/*
 * ui — OLED 状态面板的语义渲染。纯 C,主机可单测。
 */
#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void copy_trunc(char dst[UI_COLS + 1], const char *src)
{
    size_t n = 0;

    if (src) {
        while (src[n] && n < UI_COLS) {
            dst[n] = src[n];
            n++;
        }
    }
    while (n < UI_COLS) {
        dst[n++] = ' ';
    }
    dst[UI_COLS] = 0;
}

static void row_set(char rows[UI_ROWS][UI_COLS + 1], int r, const char *fmt, ...)
{
    va_list ap;

    if (r < 0 || r >= UI_ROWS) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(rows[r], UI_COLS + 1, fmt, ap);
    va_end(ap);

    size_t n = strlen(rows[r]);
    while (n < UI_COLS) {
        rows[r][n++] = ' ';
    }
    rows[r][UI_COLS] = 0;
}

static void code_hex(const uint8_t *code, char out[9])
{
    snprintf(out, 9, "%02X%02X%02X%02X", code[0], code[1], code[2], code[3]);
}

void ui_init(ui_model_t *m, const char *title, int count, int max, bool present)
{
    if (!m) {
        return;
    }
    memset(m, 0, sizeof(*m));
    copy_trunc(m->title, title ? title : "RF-Lab");
    copy_trunc(m->boot1, "");
    copy_trunc(m->boot2, "");
    copy_trunc(m->detail, "");
    m->present = present;
    m->enabled = true;
    m->ev = UI_EV_IDLE;
    m->count = count;
    m->max = max;
    m->band = RF34DL_BAND_315;
}

void ui_note_boot(ui_model_t *m, const char *line1, const char *line2)
{
    if (!m) {
        return;
    }
    copy_trunc(m->boot1, line1);
    copy_trunc(m->boot2, line2);
    m->ev = UI_EV_BOOT;
}

void ui_note_idle(ui_model_t *m)
{
    if (m) {
        m->ev = UI_EV_IDLE;
    }
}

void ui_note_seed(ui_model_t *m, int id, uint8_t band, const uint8_t code[RF34DL_CODE_LEN])
{
    if (!m) {
        return;
    }
    m->last_id = id;
    m->band = band;
    memcpy(m->code, code, RF34DL_CODE_LEN);
    m->has_code = true;
}

void ui_note_forget(ui_model_t *m)
{
    if (!m) {
        return;
    }
    m->has_code = false;
    m->last_id = 0;
}

void ui_note_learn(ui_model_t *m, int id, uint8_t band, const uint8_t code[RF34DL_CODE_LEN])
{
    if (!m) {
        return;
    }
    ui_note_seed(m, id, band, code);
    m->ev = UI_EV_LEARN;
}

void ui_note_rx(ui_model_t *m, int id, uint8_t band, const uint8_t code[RF34DL_CODE_LEN])
{
    if (!m) {
        return;
    }
    ui_note_seed(m, id, band, code);
    m->ev = UI_EV_RX;
}

void ui_note_full(ui_model_t *m, uint8_t band, const uint8_t code[RF34DL_CODE_LEN])
{
    if (!m) {
        return;
    }
    ui_note_seed(m, 0, band, code);
    m->ev = UI_EV_FULL;
}

void ui_note_tx(ui_model_t *m, ui_tx_kind_t kind, int id, uint8_t band,
                const uint8_t code[RF34DL_CODE_LEN])
{
    if (!m) {
        return;
    }
    ui_note_seed(m, id, band, code);
    switch (kind) {
    case UI_TX_LONG:
        m->ev = UI_EV_LONG;
        break;
    case UI_TX_STOP:
        m->ev = UI_EV_STOP;
        break;
    case UI_TX_SEND:
    default:
        m->ev = UI_EV_TX_PENDING;
        break;
    }
}

void ui_note_tx_result(ui_model_t *m, bool ok, unsigned elapsed_ms)
{
    if (!m || m->ev != UI_EV_TX_PENDING) {
        return;
    }
    m->elapsed_ms = elapsed_ms;
    m->ev = ok ? UI_EV_TX_OK : UI_EV_TX_WARN;
}

void ui_note_error(ui_model_t *m, const char *detail)
{
    if (!m) {
        return;
    }
    copy_trunc(m->detail, detail ? detail : "?");
    m->ev = UI_EV_ERROR;
}

void ui_note_count(ui_model_t *m, int count)
{
    if (m) {
        m->count = count;
    }
}

void ui_note_counters(ui_model_t *m, unsigned frames, unsigned acks, unsigned stray)
{
    if (!m) {
        return;
    }
    m->frames = frames;
    m->acks = acks;
    m->stray = stray;
}

void ui_render(const ui_model_t *m, char rows[UI_ROWS][UI_COLS + 1])
{
    char hex[9] = "--------";
    const char *band = "???";

    if (!m || !rows) {
        return;
    }
    for (int r = 0; r < UI_ROWS; r++) {
        memset(rows[r], ' ', UI_COLS);
        rows[r][UI_COLS] = 0;
    }
    if (m->has_code) {
        code_hex(m->code, hex);
        band = rf34dl_band_name(m->band);
    }

    switch (m->ev) {
    case UI_EV_BOOT:
        row_set(rows, 0, "%s", m->title);
        row_set(rows, 1, "%s", m->boot1);
        row_set(rows, 2, "%s", m->boot2);
        row_set(rows, 3, "codes %d", m->count);
        break;

    case UI_EV_LEARN:
        row_set(rows, 0, "LEARN ID %d", m->last_id);
        row_set(rows, 1, "%s", band);
        row_set(rows, 2, "%s", hex);
        row_set(rows, 3, "saved %d/%d", m->count, m->max);
        break;

    case UI_EV_RX:
        row_set(rows, 0, "RX ID %d", m->last_id);
        row_set(rows, 1, "%s", band);
        row_set(rows, 2, "%s", hex);
        row_set(rows, 3, "known");
        break;

    case UI_EV_FULL:
        row_set(rows, 0, "STORE FULL");
        row_set(rows, 1, "%s", band);
        row_set(rows, 2, "%s", hex);
        row_set(rows, 3, "not saved");
        break;

    case UI_EV_TX_PENDING:
    case UI_EV_TX_OK:
    case UI_EV_TX_WARN:
        if (m->ev == UI_EV_TX_PENDING) {
            row_set(rows, 0, "TX ID %d", m->last_id);
            row_set(rows, 3, "sending...");
        } else if (m->ev == UI_EV_TX_OK) {
            row_set(rows, 0, "TX ID %d OK", m->last_id);
            row_set(rows, 3, "F0 %ums", m->elapsed_ms);
        } else {
            row_set(rows, 0, "TX ID %d WARN", m->last_id);
            row_set(rows, 3, "no F0 %ums", m->elapsed_ms);
        }
        row_set(rows, 1, "%s", band);
        row_set(rows, 2, "%s", hex);
        break;

    case UI_EV_LONG:
        row_set(rows, 0, "LONG ID %d", m->last_id);
        row_set(rows, 1, "%s", band);
        row_set(rows, 2, "%s", hex);
        row_set(rows, 3, "until STOP");
        break;

    case UI_EV_STOP:
        row_set(rows, 0, "STOP ID %d", m->last_id);
        row_set(rows, 1, "%s", band);
        row_set(rows, 2, "%s", hex);
        row_set(rows, 3, "stopping");
        break;

    case UI_EV_ERROR:
        row_set(rows, 0, "ERROR");
        row_set(rows, 1, "%s", m->detail);
        row_set(rows, 2, "codes %d", m->count);
        break;

    case UI_EV_IDLE:
    default:
        row_set(rows, 0, "%s", m->title);
        row_set(rows, 1, "READY %d/%d", m->count, m->max);
        if (m->has_code && m->last_id > 0) {
            row_set(rows, 2, "%s", hex);
            row_set(rows, 3, "%s id=%d", band, m->last_id);
        } else if (m->has_code) {
            row_set(rows, 2, "%s", hex);
            row_set(rows, 3, "%s", band);
        } else {
            row_set(rows, 2, "no code yet");
            row_set(rows, 3, "press remote");
        }
        break;
    }

    /* 最后一行固定是失败/活动度量,便于无主机时判断模块是否在出数据。 */
    row_set(rows, 4, "F%u A%u S%u", m->frames, m->acks, m->stray);
}

void ui_test_pattern(gfx_t *g)
{
    if (!g) {
        return;
    }
    gfx_clear(g);

    /* 四边各 1 px:列偏移/行偏移错了会先在这里露出来(边被裁掉)。 */
    gfx_rect(g, 0, 0, GFX_W, GFX_H, true);

    /* 非对称角块:镜像/翻转时位置会对不上。 */
    gfx_fill_rect(g, 1, 1, 4, 4, true);                 /* 左上大 */
    gfx_fill_rect(g, GFX_W - 3, 1, 2, 2, true);        /* 右上小 */
    gfx_fill_rect(g, 1, GFX_H - 3, 2, 2, true);        /* 左下小 */
    gfx_fill_rect(g, GFX_W - 5, GFX_H - 5, 4, 4, true);/* 右下大 */

    gfx_text(g, 3, 3, "RF-Lab");
    gfx_text(g, 3, 11, "72x40");
    gfx_text(g, 3, 19, "SSD1306");
    gfx_text(g, 3, 27, "COL 28");
}
