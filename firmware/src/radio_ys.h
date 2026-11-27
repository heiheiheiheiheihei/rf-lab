/*
 * radio_ys — YS-RF34DL 的射频后端(UART 固定码)。
 *
 * 0.8.0 M1a 只接发射(SEND/LONG/STOP);接收解析仍在 main.c(M1b 再迁)。
 * UART 细节通过 radio_ys_io_t 注入,便于主机单测;后端只负责帧构造与应答语义。
 */
#ifndef RFLAB_RADIO_YS_H
#define RFLAB_RADIO_YS_H

#include <stddef.h>
#include <stdint.h>

#include "radio.h"

typedef struct {
    void *ctx;
    size_t (*write)(void *ctx, const uint8_t *data, size_t len);
    uint8_t (*wait_ack)(void *ctx, uint8_t want, int timeout_ms);
    int64_t (*now_ms)(void *ctx);
} radio_ys_io_t;

typedef struct {
    radio_ys_io_t io;
    int ack_timeout_ms;
    bool inited;
} radio_ys_t;

/* 绑定 IO;ack_timeout_ms <= 0 时用 1500ms。返回 false 表示 IO 回调不全。 */
bool radio_ys_bind(radio_ys_t *ys, const radio_ys_io_t *io, int ack_timeout_ms);
const radio_ops_t *radio_ys_ops(void);

#endif /* RFLAB_RADIO_YS_H */
