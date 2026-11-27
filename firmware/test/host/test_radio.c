/*
 * radio.c / radio_ys.c 的主机单测(gcc,不依赖 ESP-IDF)。
 *
 * 目标:
 *   1. 门面的能力位/状态/错误码;
 *   2. YS 后端组帧与旧路径(rf34dl_build_tx)逐字节一致;
 *   3. SEND/LONG/STOP 的应答语义(0xF0/0xFE/0xFF)与超时。
 */
#include <stdio.h>
#include <string.h>

#include "radio.h"
#include "radio_ys.h"
#include "rf34dl.h"

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

#define CHECK_BYTES(a, b, n) CHECK(memcmp((a), (b), (n)) == 0)

/* ---------------- fake backend ---------------- */

typedef struct {
    uint32_t caps;
    int probe_calls, init_calls, deinit_calls;
    int send_calls, long_start_calls, long_stop_calls;
    int raw_start_calls, raw_stop_calls, raw_read_calls, tx_raw_calls;
    int learn_calls, cancel_calls;
    radio_err_t send_err, long_start_err, long_stop_err;
    radio_err_t raw_start_err, tx_raw_err;
    radio_err_t learn_err;
    uint8_t send_ack, long_ack, stop_ack;
    radio_fixed_t last_frame;
    radio_t *reenter_target;
    radio_err_t reenter_err;
    size_t raw_read_n;
} fake_t;

static radio_err_t f_probe(void *ctx, radio_info_t *out)
{
    fake_t *f = ctx;
    f->probe_calls++;
    out->caps = f->caps;
    out->preset_count = 0;
    out->name = "fake";
    return RADIO_OK;
}

static radio_err_t f_init(void *ctx)
{
    ((fake_t *)ctx)->init_calls++;
    return RADIO_OK;
}

static radio_err_t f_deinit(void *ctx)
{
    ((fake_t *)ctx)->deinit_calls++;
    return RADIO_OK;
}

static radio_err_t f_learn(void *ctx, radio_fixed_t *out)
{
    fake_t *f = ctx;
    f->learn_calls++;
    if (f->learn_err == RADIO_OK && out != NULL) {
        out->band = RF34DL_BAND_433;
        memset(out->code, 0, sizeof(out->code));
    }
    return f->learn_err;
}

static radio_err_t f_send(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    fake_t *f = ctx;
    f->send_calls++;
    f->last_frame = *frame;
    if (f->reenter_target != NULL) {
        radio_t *target = f->reenter_target;
        radio_tx_result_t r;
        f->reenter_target = NULL;
        f->reenter_err = radio_send(target, frame, &r);
    }
    if (res != NULL) {
        res->ack = f->send_ack;
        res->elapsed_ms = 850;
    }
    return f->send_err;
}

static radio_err_t f_long_start(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    fake_t *f = ctx;
    f->long_start_calls++;
    f->last_frame = *frame;
    if (res != NULL) {
        res->ack = f->long_ack;
        res->elapsed_ms = 170;
    }
    return f->long_start_err;
}

static radio_err_t f_long_stop(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res)
{
    fake_t *f = ctx;
    f->long_stop_calls++;
    f->last_frame = *frame;
    if (res != NULL) {
        res->ack = f->stop_ack;
        res->elapsed_ms = 23;
    }
    return f->long_stop_err;
}

static radio_err_t f_raw_start(void *ctx, const radio_raw_cfg_t *cfg)
{
    (void)cfg;
    ((fake_t *)ctx)->raw_start_calls++;
    return ((fake_t *)ctx)->raw_start_err;
}

static radio_err_t f_raw_stop(void *ctx)
{
    ((fake_t *)ctx)->raw_stop_calls++;
    return RADIO_OK;
}

static radio_err_t f_raw_read(void *ctx, int32_t *pulses, size_t cap, size_t *out_count)
{
    fake_t *f = ctx;
    f->raw_read_calls++;
    if (f->raw_read_n > cap) {
        return RADIO_ERR_ARG;
    }
    for (size_t i = 0; i < f->raw_read_n; i++) {
        pulses[i] = (i & 1) ? -320 : 960;
    }
    *out_count = f->raw_read_n;
    return RADIO_OK;
}

static radio_err_t f_tx_raw(void *ctx, const radio_raw_tx_t *tx)
{
    fake_t *f = ctx;
    f->tx_raw_calls++;
    if (tx == NULL || tx->pulses == NULL || tx->count == 0) {
        return RADIO_ERR_ARG;
    }
    return f->tx_raw_err;
}

static void f_cancel(void *ctx)
{
    ((fake_t *)ctx)->cancel_calls++;
}

static const radio_ops_t k_fake_ops = {
    .name = "fake",
    .probe = f_probe,
    .init = f_init,
    .deinit = f_deinit,
    .learn_poll = f_learn,
    .send = f_send,
    .long_start = f_long_start,
    .long_stop = f_long_stop,
    .raw_start = f_raw_start,
    .raw_stop = f_raw_stop,
    .raw_read = f_raw_read,
    .tx_raw = f_tx_raw,
    .cancel = f_cancel,
};

static const radio_fixed_t k_frame = {
    .band = RF34DL_BAND_433,
    .code = {0x12, 0x34, 0x05, 0x60},
};

static void fake_setup(fake_t *f, radio_t *r, uint32_t caps)
{
    memset(f, 0, sizeof(*f));
    f->caps = caps;
    f->send_err = RADIO_OK;
    f->long_start_err = RADIO_OK;
    f->long_stop_err = RADIO_OK;
    f->raw_start_err = RADIO_OK;
    f->tx_raw_err = RADIO_OK;
    f->send_ack = RF34DL_ACK_TX_OK;
    f->long_ack = RF34DL_ACK_LONG_ON;
    f->stop_ack = RF34DL_ACK_STOPPED;
    f->learn_err = RADIO_ERR_EMPTY;
    radio_bind(r, &k_fake_ops, f);
}

/* ---------------- 门面 ---------------- */

static void test_facade_basic(void)
{
    fake_t f;
    radio_t r;
    fake_setup(&f, &r, RADIO_CAP_FIXED_TX | RADIO_CAP_LONG);

    CHECK(r.state == RADIO_STATE_INIT);
    CHECK(radio_send(&r, &k_frame, NULL) == RADIO_ERR_STATE); /* 未 init */
    CHECK(f.send_calls == 0);

    CHECK(radio_init(&r) == RADIO_OK);
    CHECK(f.probe_calls == 1);
    CHECK(f.init_calls == 1);
    CHECK(r.inited);
    CHECK(r.state == RADIO_STATE_IDLE);
    CHECK(radio_has_cap(&r, RADIO_CAP_FIXED_TX));
    CHECK(!radio_has_cap(&r, RADIO_CAP_RAW_TX));
    CHECK(strcmp(r.info.name, "fake") == 0);

    radio_tx_result_t res = {0};
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_OK);
    CHECK(f.send_calls == 1);
    CHECK(res.ack == RF34DL_ACK_TX_OK);
    CHECK(res.elapsed_ms == 850);
    CHECK(r.state == RADIO_STATE_IDLE);

    CHECK(radio_send(&r, NULL, &res) == RADIO_ERR_ARG);

    CHECK(radio_deinit(&r) == RADIO_OK);
    CHECK(f.deinit_calls == 1);
    CHECK(!r.inited);
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_ERR_STATE);
}

static void test_facade_caps(void)
{
    fake_t f;
    radio_t r;
    fake_setup(&f, &r, 0);
    CHECK(radio_init(&r) == RADIO_OK);
    CHECK(radio_send(&r, &k_frame, NULL) == RADIO_ERR_UNSUPPORTED);
    CHECK(radio_long_start(&r, &k_frame, NULL) == RADIO_ERR_UNSUPPORTED);
    CHECK(radio_raw_start(&r, &(radio_raw_cfg_t){0}) == RADIO_ERR_UNSUPPORTED);
    CHECK(radio_learn_poll(&r, NULL) == RADIO_ERR_UNSUPPORTED);
    CHECK(f.send_calls == 0);
}

static void test_facade_reentrant_busy(void)
{
    fake_t f;
    radio_t r;
    fake_setup(&f, &r, RADIO_CAP_FIXED_TX);
    CHECK(radio_init(&r) == RADIO_OK);

    f.reenter_target = &r;
    radio_tx_result_t res = {0};
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_OK);
    CHECK(f.reenter_err == RADIO_ERR_BUSY);
    CHECK(r.state == RADIO_STATE_IDLE);
}

static void test_facade_long(void)
{
    fake_t f;
    radio_t r;
    fake_setup(&f, &r, RADIO_CAP_FIXED_TX | RADIO_CAP_LONG);
    CHECK(radio_init(&r) == RADIO_OK);

    radio_tx_result_t res = {0};
    CHECK(radio_long_start(&r, &k_frame, &res) == RADIO_OK);
    CHECK(res.ack == RF34DL_ACK_LONG_ON);
    CHECK(r.state == RADIO_STATE_LONG);
    CHECK(f.last_frame.band == RF34DL_BAND_433);

    /* 0.7.x 行为:LONG 中仍可 SEND;SEND 结束后回 IDLE(旧 s_tx_active=false)。 */
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_OK);
    CHECK(r.state == RADIO_STATE_IDLE);

    /* STOP 不要求先 LONG(旧 CLI 允许直接 STOP)。 */
    CHECK(radio_long_stop(&r, &k_frame, &res) == RADIO_OK);
    CHECK(res.ack == RF34DL_ACK_STOPPED);
    CHECK(r.state == RADIO_STATE_IDLE);

    /* long_start 超时 => 不进入 LONG,与旧固件 s_tx_active 判定一致。 */
    r.state = RADIO_STATE_IDLE;
    f.long_start_err = RADIO_ERR_TIMEOUT;
    CHECK(radio_long_start(&r, &k_frame, &res) == RADIO_ERR_TIMEOUT);
    CHECK(r.state == RADIO_STATE_IDLE);
}

static void test_facade_learn(void)
{
    fake_t f;
    radio_t r;
    fake_setup(&f, &r, RADIO_CAP_FIXED_RX);
    CHECK(radio_init(&r) == RADIO_OK);

    radio_fixed_t out;
    CHECK(radio_learn_poll(&r, &out) == RADIO_ERR_EMPTY);
    CHECK(f.learn_calls == 1);

    f.learn_err = RADIO_OK;
    CHECK(radio_learn_poll(&r, &out) == RADIO_OK);
    CHECK(out.band == RF34DL_BAND_433);
}

static void test_facade_raw(void)
{
    fake_t f;
    radio_t r;
    fake_setup(&f, &r, RADIO_CAP_RAW_RX | RADIO_CAP_RAW_TX | RADIO_CAP_LONG);
    CHECK(radio_init(&r) == RADIO_OK);

    radio_raw_cfg_t cfg = {.freq_hz = 433920000, .preset = 0, .sensitivity = -1, .power = -1};
    CHECK(radio_raw_start(&r, &cfg) == RADIO_OK);
    CHECK(r.state == RADIO_STATE_RAW);

    int32_t buf[8];
    size_t n = 0;
    f.raw_read_n = 4;
    CHECK(radio_raw_read(&r, buf, 8, &n) == RADIO_OK);
    CHECK(n == 4);
    CHECK(buf[0] == 960 && buf[1] == -320);

    int32_t pulses[4] = {100, -100, 100, -100};
    radio_raw_tx_t tx = {.pulses = pulses, .count = 4};
    CHECK(radio_tx_raw(&r, &tx) == RADIO_ERR_BUSY); /* 采集中不允许发 */

    CHECK(radio_raw_stop(&r) == RADIO_OK);
    CHECK(r.state == RADIO_STATE_IDLE);
    CHECK(radio_tx_raw(&r, &tx) == RADIO_OK);
    CHECK(f.tx_raw_calls == 1); /* BUSY 那次在门面被挡,没进后端 */
    CHECK(r.state == RADIO_STATE_IDLE);

    tx.count = 0;
    CHECK(radio_tx_raw(&r, &tx) == RADIO_ERR_ARG);
    tx.count = RADIO_RAW_MAX_PULSES + 1;
    CHECK(radio_tx_raw(&r, &tx) == RADIO_ERR_ARG);

    /* LONG 中不允许开 RAW(单射频前端)。 */
    f.tx_raw_err = RADIO_OK;
    CHECK(radio_long_start(&r, &k_frame, NULL) == RADIO_OK);
    CHECK(radio_raw_start(&r, &cfg) == RADIO_ERR_BUSY);

    radio_cancel(&r);
    CHECK(f.cancel_calls == 1);
}

/* ---------------- YS 后端 ---------------- */

typedef struct {
    uint8_t bytes[64];
    size_t len;
    uint8_t ack;
    uint8_t want;
    int wait_calls;
    int64_t now;
    int64_t advance_ms;
    size_t short_write;
    int timeout_arg;
} io_fake_t;

static size_t io_write(void *ctx, const uint8_t *data, size_t len)
{
    io_fake_t *io = ctx;
    size_t n = len;
    if (io->short_write > 0 && io->short_write < n) {
        n = io->short_write;
    }
    if (io->len + n <= sizeof(io->bytes)) {
        memcpy(io->bytes + io->len, data, n);
        io->len += n;
    }
    return n;
}

static uint8_t io_wait_ack(void *ctx, uint8_t want, int timeout_ms)
{
    io_fake_t *io = ctx;
    io->wait_calls++;
    io->want = want;
    io->timeout_arg = timeout_ms;
    io->now += io->advance_ms;
    return io->ack;
}

static int64_t io_now(void *ctx)
{
    return ((io_fake_t *)ctx)->now;
}

static void ys_setup(io_fake_t *io, radio_ys_t *ys, radio_t *r)
{
    memset(io, 0, sizeof(*io));
    io->advance_ms = 850;
    io->ack = RF34DL_ACK_TX_OK;
    radio_ys_io_t cfg = {
        .ctx = io,
        .write = io_write,
        .wait_ack = io_wait_ack,
        .now_ms = io_now,
    };
    CHECK(radio_ys_bind(ys, &cfg, 1500));
    radio_bind(r, radio_ys_ops(), ys);
    CHECK(radio_init(r) == RADIO_OK);
}

static void test_ys_frames(void)
{
    io_fake_t io;
    radio_ys_t ys;
    radio_t r;
    ys_setup(&io, &ys, &r);

    CHECK(io.timeout_arg == 0); /* 还没发过 */
    CHECK(r.info.caps == (RADIO_CAP_FIXED_TX | RADIO_CAP_LONG));
    CHECK(strcmp(r.info.name, "YS-RF34DL") == 0);

    radio_tx_result_t res = {0};
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_OK);
    CHECK(io.len == RF34DL_FRAME_LEN);
    CHECK(io.want == RF34DL_ACK_TX_OK);
    CHECK(io.timeout_arg == 1500);
    CHECK(res.elapsed_ms == 850);
    CHECK(io.bytes[0] == 0xFD && io.bytes[1] == 0xF4 && io.bytes[2] == 0x03);
    CHECK(io.bytes[7] == 0xDF);

    /* 与旧路径逐字节一致:同一个 rf34dl_build_tx 输出。 */
    for (int band = 0; band < 2; band++) {
        uint8_t expect[RF34DL_FRAME_LEN];
        uint8_t b = band == 0 ? RF34DL_BAND_315 : RF34DL_BAND_433;
        radio_fixed_t f = {.band = b, .code = {0x55, 0x55, 0x03, 0x3C}};
        memset(&io, 0, sizeof(io));
        io.advance_ms = 850;
        io.ack = RF34DL_ACK_TX_OK;
        CHECK(rf34dl_build_tx(expect, b, RF34DL_DUR_NORMAL, f.code) == RF34DL_FRAME_LEN);
        CHECK(radio_send(&r, &f, &res) == RADIO_OK);
        CHECK(io.len == RF34DL_FRAME_LEN);
        CHECK_BYTES(io.bytes, expect, RF34DL_FRAME_LEN);
    }
}

static void test_ys_long_stop(void)
{
    io_fake_t io;
    radio_ys_t ys;
    radio_t r;
    ys_setup(&io, &ys, &r);

    io.ack = RF34DL_ACK_LONG_ON;
    radio_tx_result_t res = {0};
    CHECK(radio_long_start(&r, &k_frame, &res) == RADIO_OK);
    CHECK(io.want == RF34DL_ACK_LONG_ON);
    CHECK(io.len == 8 && io.bytes[2] == RF34DL_DUR_LONG);
    CHECK(res.ack == RF34DL_ACK_LONG_ON);
    CHECK(r.state == RADIO_STATE_LONG);

    memset(&io, 0, sizeof(io));
    io.advance_ms = 23;
    io.ack = RF34DL_ACK_STOPPED;
    CHECK(radio_long_stop(&r, &k_frame, &res) == RADIO_OK);
    CHECK(io.want == RF34DL_ACK_STOPPED);
    CHECK(io.bytes[2] == RF34DL_DUR_STOP);
    CHECK(res.elapsed_ms == 23);
    CHECK(r.state == RADIO_STATE_IDLE);
}

static void test_ys_errors(void)
{
    io_fake_t io;
    radio_ys_t ys;
    radio_t r;
    ys_setup(&io, &ys, &r);

    /* 应答不匹配:超时,ack 保留最后收到的值。 */
    memset(&io, 0, sizeof(io));
    io.ack = 0x00;
    io.advance_ms = 1500;
    radio_tx_result_t res = {0};
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_ERR_TIMEOUT);
    CHECK(res.ack == 0x00);
    CHECK(res.elapsed_ms == 1500);

    /* 写不足 8 字节:立即 IO 错,不再等应答。 */
    memset(&io, 0, sizeof(io));
    io.short_write = 3;
    CHECK(radio_send(&r, &k_frame, &res) == RADIO_ERR_IO);
    CHECK(io.wait_calls == 0);

    /* 非法 band 在组帧前被拒。 */
    radio_fixed_t bad = {.band = 0x00, .code = {1, 2, 3, 4}};
    memset(&io, 0, sizeof(io));
    CHECK(radio_send(&r, &bad, &res) == RADIO_ERR_ARG);
    CHECK(io.len == 0);
    CHECK(io.wait_calls == 0);

    /* 未实现的原始脉冲能力。 */
    radio_raw_cfg_t cfg = {0};
    CHECK(radio_raw_start(&r, &cfg) == RADIO_ERR_UNSUPPORTED);
    int32_t pulses[4] = {1, -1, 1, -1};
    radio_raw_tx_t tx = {.pulses = pulses, .count = 4};
    CHECK(radio_tx_raw(&r, &tx) == RADIO_ERR_UNSUPPORTED);

    /* learn_poll 未实现(能力位也没置)。 */
    radio_fixed_t out;
    CHECK(radio_learn_poll(&r, &out) == RADIO_ERR_UNSUPPORTED);
}

static void test_ys_bind(void)
{
    radio_ys_t ys;
    radio_ys_io_t io = {0};
    CHECK(!radio_ys_bind(&ys, &io, 1500));
    CHECK(!radio_ys_bind(NULL, &io, 1500));
    CHECK(!radio_ys_bind(&ys, NULL, 1500));

    io.ctx = NULL;
    io.write = io_write;
    CHECK(!radio_ys_bind(&ys, &io, 1500)); /* 缺 wait/now */
}

int main(void)
{
    test_facade_basic();
    test_facade_caps();
    test_facade_reentrant_busy();
    test_facade_long();
    test_facade_learn();
    test_facade_raw();
    test_ys_frames();
    test_ys_long_stop();
    test_ys_errors();
    test_ys_bind();

    printf("%d checks, %d failure(s)\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
