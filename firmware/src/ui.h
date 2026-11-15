/*
 * ui — OLED 状态面板的「语义 → 5 行 × 12 字符」映射。
 *
 * 纯 C,不依赖 ESP-IDF。屏幕是纯输出设备(本板没有可用按键),所以这里不建模菜单,
 * 只把串口侧发生的事件变成一眼可读的状态页。I2C 与 SSD1306 在 oled.c。
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#include "oled_gfx.h"
#include "rf34dl.h"

#define UI_COLS 12
#define UI_ROWS 5

#if UI_ROWS != GFX_PAGES
#error "UI_ROWS 必须等于帧缓冲页数(每行 8 px)"
#endif

typedef enum {
    UI_EV_IDLE = 0,
    UI_EV_BOOT,
    UI_EV_LEARN,
    UI_EV_RX,
    UI_EV_FULL,
    UI_EV_TX_PENDING,
    UI_EV_TX_OK,
    UI_EV_TX_WARN,
    UI_EV_LONG,
    UI_EV_STOP,
    UI_EV_ERROR,
} ui_event_t;

typedef enum {
    UI_TX_SEND = 0, /* SEND:单次发射 */
    UI_TX_LONG,     /* LONG:长发 */
    UI_TX_STOP,     /* STOP:停止 */
} ui_tx_kind_t;

typedef struct {
    bool present; /* I2C probe 是否找到屏 */
    bool enabled; /* DISP ON/OFF */
    ui_event_t ev;
    char title[UI_COLS + 1]; /* 空闲页标题(带固件版本),由 main 传入 */
    char boot1[UI_COLS + 1];
    char boot2[UI_COLS + 1];
    char detail[UI_COLS + 1]; /* ERROR 的第一行 */
    int count;
    int max;
    int last_id;
    uint8_t band;
    uint8_t code[RF34DL_CODE_LEN];
    bool has_code;
    unsigned frames;
    unsigned acks;
    unsigned stray;
    unsigned elapsed_ms; /* TX 结果耗时 */
} ui_model_t;

void ui_init(ui_model_t *m, const char *title, int count, int max, bool present);
void ui_note_boot(ui_model_t *m, const char *line1, const char *line2);
void ui_note_idle(ui_model_t *m);
/* 开机时把 NVS 里最后一条码填进空闲页(不改变事件类型)。 */
void ui_note_seed(ui_model_t *m, int id, uint8_t band, const uint8_t code[RF34DL_CODE_LEN]);
void ui_note_forget(ui_model_t *m);
void ui_note_learn(ui_model_t *m, int id, uint8_t band, const uint8_t code[RF34DL_CODE_LEN]);
void ui_note_rx(ui_model_t *m, int id, uint8_t band, const uint8_t code[RF34DL_CODE_LEN]);
void ui_note_full(ui_model_t *m, uint8_t band, const uint8_t code[RF34DL_CODE_LEN]);
void ui_note_tx(ui_model_t *m, ui_tx_kind_t kind, int id, uint8_t band,
                const uint8_t code[RF34DL_CODE_LEN]);
void ui_note_tx_result(ui_model_t *m, bool ok, unsigned elapsed_ms);
void ui_note_error(ui_model_t *m, const char *detail);
void ui_note_count(ui_model_t *m, int count);
void ui_note_counters(ui_model_t *m, unsigned frames, unsigned acks, unsigned stray);

/* 渲染成 5 行,每行固定 12 字符 + NUL(不足补空格)。 */
void ui_render(const ui_model_t *m, char rows[UI_ROWS][UI_COLS + 1]);

/* 几何/方向自检图案:四边框、非对称角块、文字。用于 OLED TEST 与上机验收。 */
void ui_test_pattern(gfx_t *g);

#endif /* UI_H */
