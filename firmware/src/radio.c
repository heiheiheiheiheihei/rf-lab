/* radio.c — 射频门面:能力位、状态、错误翻译。纯 C,可在主机上单测。 */
#include "radio.h"

#include <string.h>

void radio_bind(radio_t *r, const radio_ops_t *ops, void *ctx)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->ops = ops;
    r->ctx = ctx;
    r->state = RADIO_STATE_INIT;
    r->inited = false;
}

const char *radio_err_name(radio_err_t err)
{
    switch (err) {
    case RADIO_OK:
        return "ok";
    case RADIO_ERR_ARG:
        return "arg";
    case RADIO_ERR_IO:
        return "io";
    case RADIO_ERR_BUSY:
        return "busy";
    case RADIO_ERR_STATE:
        return "state";
    case RADIO_ERR_UNSUPPORTED:
        return "unsupported";
    case RADIO_ERR_TIMEOUT:
        return "timeout";
    case RADIO_ERR_EMPTY:
        return "empty";
    case RADIO_ERR_FULL:
        return "full";
    default:
        return "unknown";
    }
}

const char *radio_state_name(radio_state_t st)
{
    switch (st) {
    case RADIO_STATE_INIT:
        return "init";
    case RADIO_STATE_IDLE:
        return "idle";
    case RADIO_STATE_TX:
        return "tx";
    case RADIO_STATE_LONG:
        return "long";
    case RADIO_STATE_RAW:
        return "raw";
    default:
        return "?";
    }
}

static bool ops_ready(const radio_t *r)
{
    return r != NULL && r->ops != NULL;
}

radio_err_t radio_probe(radio_t *r)
{
    if (!ops_ready(r) || r->ops->probe == NULL) {
        return RADIO_ERR_UNSUPPORTED;
    }
    radio_info_t info;
    memset(&info, 0, sizeof(info));
    radio_err_t e = r->ops->probe(r->ctx, &info);
    if (e == RADIO_OK) {
        r->info = info;
    }
    return e;
}

radio_err_t radio_init(radio_t *r)
{
    if (!ops_ready(r) || r->ops->init == NULL) {
        return RADIO_ERR_UNSUPPORTED;
    }
    if (r->inited) {
        return RADIO_OK;
    }
    /* init 前先 probe,保证能力位可用;probe 失败不阻断 init(YS 恒可用)。 */
    if (r->ops->probe != NULL) {
        (void)radio_probe(r);
    }
    radio_err_t e = r->ops->init(r->ctx);
    if (e == RADIO_OK) {
        r->inited = true;
        r->state = RADIO_STATE_IDLE;
    }
    return e;
}

radio_err_t radio_deinit(radio_t *r)
{
    if (!ops_ready(r)) {
        return RADIO_ERR_STATE;
    }
    if (!r->inited) {
        return RADIO_OK;
    }
    radio_err_t e = r->ops->deinit != NULL ? r->ops->deinit(r->ctx) : RADIO_OK;
    r->inited = false;
    r->state = RADIO_STATE_INIT;
    return e;
}

bool radio_has_cap(const radio_t *r, uint32_t cap)
{
    return r != NULL && (r->info.caps & cap) != 0;
}

uint32_t radio_caps(const radio_t *r)
{
    return r != NULL ? r->info.caps : 0;
}

/* 公共前置检查:已 init、能力位、非阻塞发送中 */
static radio_err_t gate(const radio_t *r, uint32_t cap, bool allow_long)
{
    if (!ops_ready(r) || !r->inited) {
        return RADIO_ERR_STATE;
    }
    if (!radio_has_cap(r, cap)) {
        return RADIO_ERR_UNSUPPORTED;
    }
    if (r->state == RADIO_STATE_TX) {
        return RADIO_ERR_BUSY;
    }
    if (!allow_long && r->state == RADIO_STATE_LONG) {
        return RADIO_ERR_BUSY;
    }
    return RADIO_OK;
}

radio_err_t radio_learn_poll(radio_t *r, radio_fixed_t *out)
{
    radio_err_t e = gate(r, RADIO_CAP_FIXED_RX, true);
    if (e != RADIO_OK) {
        return e;
    }
    if (r->ops->learn_poll == NULL || out == NULL) {
        return RADIO_ERR_UNSUPPORTED;
    }
    return r->ops->learn_poll(r->ctx, out);
}

radio_err_t radio_send(radio_t *r, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    /* 固定码发射允许在 LONG 中进行:模块自己决定优先级(0.7.x 既有行为)。 */
    radio_err_t e = gate(r, RADIO_CAP_FIXED_TX, true);
    if (e != RADIO_OK) {
        return e;
    }
    if (frame == NULL || r->ops->send == NULL) {
        return RADIO_ERR_ARG;
    }
    r->state = RADIO_STATE_TX;
    e = r->ops->send(r->ctx, frame, res);
    /* 旧固件在 LONG 中 SEND 后同样把 s_tx_active 清零,这里保持一致。 */
    r->state = RADIO_STATE_IDLE;
    return e;
}

radio_err_t radio_long_start(radio_t *r, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    radio_err_t e = gate(r, RADIO_CAP_LONG, true);
    if (e != RADIO_OK) {
        return e;
    }
    if (frame == NULL || r->ops->long_start == NULL) {
        return RADIO_ERR_ARG;
    }
    r->state = RADIO_STATE_TX;
    e = r->ops->long_start(r->ctx, frame, res);
    /* 只有收到 0xFE 才算真的进入长发;超时/失败回到 IDLE,与旧固件一致。 */
    r->state = (e == RADIO_OK) ? RADIO_STATE_LONG : RADIO_STATE_IDLE;
    return e;
}

radio_err_t radio_long_stop(radio_t *r, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    /* STOP 允许在没有 LONG 的情况下发(旧固件就是这样),因此不要求先 long_start。 */
    radio_err_t e = gate(r, RADIO_CAP_LONG, true);
    if (e != RADIO_OK) {
        return e;
    }
    if (frame == NULL || r->ops->long_stop == NULL) {
        return RADIO_ERR_ARG;
    }
    r->state = RADIO_STATE_TX;
    e = r->ops->long_stop(r->ctx, frame, res);
    r->state = RADIO_STATE_IDLE;
    return e;
}

radio_err_t radio_raw_start(radio_t *r, const radio_raw_cfg_t *cfg)
{
    radio_err_t e = gate(r, RADIO_CAP_RAW_RX, false);
    if (e != RADIO_OK) {
        return e;
    }
    if (cfg == NULL || r->ops->raw_start == NULL) {
        return RADIO_ERR_ARG;
    }
    if (r->state != RADIO_STATE_IDLE) {
        return RADIO_ERR_BUSY;
    }
    e = r->ops->raw_start(r->ctx, cfg);
    if (e == RADIO_OK) {
        r->state = RADIO_STATE_RAW;
    }
    return e;
}

radio_err_t radio_raw_stop(radio_t *r)
{
    radio_err_t e = gate(r, RADIO_CAP_RAW_RX, true);
    if (e != RADIO_OK) {
        return e;
    }
    if (r->state != RADIO_STATE_RAW || r->ops->raw_stop == NULL) {
        return RADIO_ERR_STATE;
    }
    e = r->ops->raw_stop(r->ctx);
    r->state = RADIO_STATE_IDLE;
    return e;
}

radio_err_t radio_raw_read(radio_t *r, int32_t *pulses, size_t cap, size_t *out_count)
{
    radio_err_t e = gate(r, RADIO_CAP_RAW_RX, true);
    if (e != RADIO_OK) {
        return e;
    }
    if (r->state != RADIO_STATE_RAW) {
        return RADIO_ERR_STATE;
    }
    if (pulses == NULL || out_count == NULL || r->ops->raw_read == NULL) {
        return RADIO_ERR_ARG;
    }
    return r->ops->raw_read(r->ctx, pulses, cap, out_count);
}

radio_err_t radio_tx_raw(radio_t *r, const radio_raw_tx_t *tx)
{
    radio_err_t e = gate(r, RADIO_CAP_RAW_TX, false);
    if (e != RADIO_OK) {
        return e;
    }
    if (tx == NULL || tx->pulses == NULL || tx->count == 0 ||
        tx->count > RADIO_RAW_MAX_PULSES || r->ops->tx_raw == NULL) {
        return RADIO_ERR_ARG;
    }
    if (r->state != RADIO_STATE_IDLE) {
        return RADIO_ERR_BUSY;
    }
    r->state = RADIO_STATE_TX;
    e = r->ops->tx_raw(r->ctx, tx);
    r->state = RADIO_STATE_IDLE;
    return e;
}

void radio_cancel(radio_t *r)
{
    if (ops_ready(r) && r->ops->cancel != NULL) {
        r->ops->cancel(r->ctx);
    }
}
