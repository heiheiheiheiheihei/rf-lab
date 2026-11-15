/*
 * oled — SSD1306 72×40 的 I2C 传输层(仅 ESP-IDF)。
 *
 * 职责边界:
 *   · 只做 probe / 初始化 / 写显存 / 开关屏 / 统计,不决定显示内容;
 *   · 内容由 ui.c 渲染进 oled_gfx 帧缓冲,main.c 负责调度;
 *   · 初始化失败只返回错误,不阻塞、不重启、不反复重试 —— 没有屏也要能用串口。
 */
#ifndef OLED_H
#define OLED_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "oled_gfx.h"

typedef struct {
    uint32_t flush_count; /* 成功写屏次数 */
    uint32_t flush_err;   /* 写屏失败次数 */
    uint32_t last_us;     /* 最近一次 flush 耗时(µs) */
    uint32_t max_us;      /* 最大 flush 耗时(µs) */
} oled_stats_t;

/*
 * 探测并初始化屏。sda/scl 由调用方给(硬件接线的唯一权威在 hardware/README.md)。
 * 返回:
 *   ESP_OK                 屏在线并完成初始化
 *   ESP_ERR_NOT_FOUND      I2C 上 0x3C 不应答
 *   其他                   I2C 驱动/传输错误
 * 失败后 oled_is_ready() 为 false,其余接口返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t oled_init(int sda_gpio, int scl_gpio, int scl_hz);

bool oled_is_ready(void);
/* 实际使用的 I2C 速率(初始化失败返回 0)。 */
int oled_bus_hz(void);

/* 把 360 字节帧缓冲整帧写到显存(列 28..99、页 0..4)。 */
esp_err_t oled_flush(const gfx_t *g);

/* 0xAE / 0xAF:关屏/开屏,显存内容保留。 */
esp_err_t oled_set_on(bool on);

const oled_stats_t *oled_stats(void);

#endif /* OLED_H */
