/* radio_ys.c — YS-RF34DL 后端:组帧 + 写 UART + 等应答。纯 C,主机可测。 */
#include "radio_ys.h"

#include <string.h>

#include "rf34dl.h"

#define YS_DEFAULT_ACK_TIMEOUT_MS 1500

static radio_ys_t *ys_of(void *ctx)
{
    return (radio_ys_t *)ctx;
}

static bool io_ready(const radio_ys_t *ys)
{
    return ys != NULL && ys->io.write != NULL && ys->io.wait_ack != NULL &&
           ys->io.now_ms != NULL;
}

static radio_err_t ys_probe(void *ctx, radio_info_t *out)
{
    radio_ys_t *ys = ys_of(ctx);
    if (ys == NULL || out == NULL || !io_ready(ys)) {
        return RADIO_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    /* M1a:接收解析仍在 main.c,所以暂不置 FIXED_RX(见 radio.h 头注释)。 */
    out->caps = RADIO_CAP_FIXED_TX | RADIO_CAP_LONG;
    out->preset_count = 0;
    out->name = "YS-RF34DL";
    return RADIO_OK;
}

static radio_err_t ys_init(void *ctx)
{
    radio_ys_t *ys = ys_of(ctx);
    if (ys == NULL) {
        return RADIO_ERR_ARG;
    }
    if (!io_ready(ys)) {
        return RADIO_ERR_IO;
    }
    if (ys->ack_timeout_ms <= 0) {
        ys->ack_timeout_ms = YS_DEFAULT_ACK_TIMEOUT_MS;
    }
    ys->inited = true;
    return RADIO_OK;
}

static radio_err_t ys_deinit(void *ctx)
{
    radio_ys_t *ys = ys_of(ctx);
    if (ys != NULL) {
        ys->inited = false;
    }
    return RADIO_OK;
}

/* 统一发射:build -> write -> wait ack。 */
static radio_err_t ys_tx(radio_ys_t *ys, const radio_fixed_t *frame, uint8_t dur,
                         uint8_t want, radio_tx_result_t *res)
{
    uint8_t buf[RF34DL_FRAME_LEN];

    if (res != NULL) {
        res->ack = 0;
        res->elapsed_ms = 0;
    }
    if (ys == NULL || frame == NULL) {
        return RADIO_ERR_ARG;
    }
    if (!ys->inited) {
        return RADIO_ERR_STATE;
    }
    if (!rf34dl_band_ok(frame->band)) {
        return RADIO_ERR_ARG;
    }
    if (rf34dl_build_tx(buf, frame->band, dur, frame->code) != RF34DL_FRAME_LEN) {
        return RADIO_ERR_ARG;
    }

    size_t written = ys->io.write(ys->io.ctx, buf, sizeof(buf));
    if (written != sizeof(buf)) {
        return RADIO_ERR_IO;
    }

    int64_t t0 = ys->io.now_ms(ys->io.ctx);
    uint8_t ack = ys->io.wait_ack(ys->io.ctx, want, ys->ack_timeout_ms);
    uint32_t elapsed = (uint32_t)(ys->io.now_ms(ys->io.ctx) - t0);

    if (res != NULL) {
        res->ack = ack;
        res->elapsed_ms = elapsed;
    }
    return ack == want ? RADIO_OK : RADIO_ERR_TIMEOUT;
}

static radio_err_t ys_send(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    return ys_tx(ys_of(ctx), frame, RF34DL_DUR_NORMAL, RF34DL_ACK_TX_OK, res);
}

static radio_err_t ys_long_start(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    return ys_tx(ys_of(ctx), frame, RF34DL_DUR_LONG, RF34DL_ACK_LONG_ON, res);
}

static radio_err_t ys_long_stop(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    return ys_tx(ys_of(ctx), frame, RF34DL_DUR_STOP, RF34DL_ACK_STOPPED, res);
}

static radio_err_t ys_learn_poll(void *ctx, radio_fixed_t *out)
{
    (void)ctx;
    (void)out;
    return RADIO_ERR_UNSUPPORTED; /* M1b:解析迁入后端后再实现 */
}

static radio_err_t ys_raw_start(void *ctx, const radio_raw_cfg_t *cfg)
{
    (void)ctx;
    (void)cfg;
    return RADIO_ERR_UNSUPPORTED;
}

static radio_err_t ys_raw_stop(void *ctx)
{
    (void)ctx;
    return RADIO_ERR_UNSUPPORTED;
}

static radio_err_t ys_raw_read(void *ctx, int32_t *pulses, size_t cap, size_t *out_count)
{
    (void)ctx;
    (void)pulses;
    (void)cap;
    (void)out_count;
    return RADIO_ERR_UNSUPPORTED;
}

static radio_err_t ys_tx_raw(void *ctx, const radio_raw_tx_t *tx)
{
    (void)ctx;
    (void)tx;
    return RADIO_ERR_UNSUPPORTED;
}

static void ys_cancel(void *ctx)
{
    (void)ctx;
}

static const radio_ops_t k_ys_ops = {
    .name = "ys-rf34dl",
    .probe = ys_probe,
    .init = ys_init,
    .deinit = ys_deinit,
    .learn_poll = ys_learn_poll,
    .send = ys_send,
    .long_start = ys_long_start,
    .long_stop = ys_long_stop,
    .raw_start = ys_raw_start,
    .raw_stop = ys_raw_stop,
    .raw_read = ys_raw_read,
    .tx_raw = ys_tx_raw,
    .cancel = ys_cancel,
};

const radio_ops_t *radio_ys_ops(void)
{
    return &k_ys_ops;
}

bool radio_ys_bind(radio_ys_t *ys, const radio_ys_io_t *io, int ack_timeout_ms)
{
    if (ys == NULL || io == NULL) {
        return false;
    }
    memset(ys, 0, sizeof(*ys));
    ys->io = *io;
    ys->ack_timeout_ms = ack_timeout_ms > 0 ? ack_timeout_ms : YS_DEFAULT_ACK_TIMEOUT_MS;
    ys->inited = false;
    return io_ready(ys);
}
