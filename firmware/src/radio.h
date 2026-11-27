/*
 * radio — 射频后端门面(与具体模块无关的纯 C 层)。
 *
 * 设计依据:docs/adr/ADR-001-radio-hal.md
 *
 * 职责:
 *   - 能力位(FIXED_RX / FIXED_TX / LONG / RAW_RX / RAW_TX / RSSI / ...)
 *   - 状态与互斥(IDLE / TX / LONG / RAW)
 *   - 把后端的错误翻成统一 radio_err_t
 * 不负责:
 *   - UART/SPI 细节、寄存器、命令语法、UI
 *   - 命令语义(仍由 main.c 决定,保证 SEND/LONG/STOP 输出不变)
 *
 * 0.8.0 M1a:只接 YS 的 TX 路径;RX 解析暂留在 main.c(M1b 再迁)。
 * 因此 YS 后端的能力位暂不置 FIXED_RX。
 */
#ifndef RFLAB_RADIO_H
#define RFLAB_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 能力位 —— 与 ADR-001 §2.2 一致 */
#define RADIO_CAP_FIXED_RX (1u << 0)
#define RADIO_CAP_FIXED_TX (1u << 1)
#define RADIO_CAP_LONG (1u << 2)
#define RADIO_CAP_RAW_RX (1u << 3)
#define RADIO_CAP_RAW_TX (1u << 4)
#define RADIO_CAP_RSSI (1u << 5)
#define RADIO_CAP_POWER (1u << 6)
#define RADIO_CAP_SENS (1u << 7)
#define RADIO_CAP_SCAN (1u << 8)

/* 原始脉冲上限(段);缓冲 4,096×4B = 16KB,见 ADR-003 §2.2 */
#define RADIO_RAW_MAX_PULSES 4096

typedef enum {
    RADIO_OK = 0,
    RADIO_ERR_ARG,         /* 参数非法(band/code/段数) */
    RADIO_ERR_IO,          /* 写 UART/SPI 失败 */
    RADIO_ERR_BUSY,        /* 正在收/发,拒绝新动作 */
    RADIO_ERR_STATE,       /* 未 init / 状态不允许 */
    RADIO_ERR_UNSUPPORTED, /* 能力位未置位 */
    RADIO_ERR_TIMEOUT,     /* 等应答/等完成超时 */
    RADIO_ERR_EMPTY,       /* 非阻塞轮询:暂无数据(不是错误) */
    RADIO_ERR_FULL,        /* 会话缓冲满 */
} radio_err_t;

typedef enum {
    RADIO_STATE_INIT = 0, /* 已 bind,未 init */
    RADIO_STATE_IDLE,
    RADIO_STATE_TX,   /* 单次发射(阻塞) */
    RADIO_STATE_LONG, /* 长发进行中 */
    RADIO_STATE_RAW,  /* 原始接收会话 */
} radio_state_t;

/* 固定码:band 为 YS 语义(0xF3/0xF4),code[4] = 地址2/地址1/按键/osc */
typedef struct {
    uint8_t band;
    uint8_t code[4];
} radio_fixed_t;

typedef struct {
    uint32_t freq_hz;    /* 0 = 模块自有频段 */
    uint8_t preset;      /* 后端预设编号;0xFF = 默认 */
    int8_t sensitivity;  /* -1 = 默认;0..2 */
    int8_t power;        /* -1 = 默认;0..7 */
} radio_raw_cfg_t;

typedef struct {
    const int32_t *pulses; /* 有符号 µs,正 = 载波开 */
    size_t count;          /* 1..RADIO_RAW_MAX_PULSES */
    uint32_t freq_hz;
    uint8_t preset;
    int8_t power;
    uint8_t repeat;        /* 0/1 = 一遍 */
} radio_raw_tx_t;

typedef struct {
    uint32_t caps;
    uint8_t preset_count;
    const char *name;
} radio_info_t;

/* 一次发射的结果:后端填 ack(0 = 没收到)与耗时 */
typedef struct {
    uint8_t ack;
    uint32_t elapsed_ms;
} radio_tx_result_t;

typedef struct {
    const char *name;
    radio_err_t (*probe)(void *ctx, radio_info_t *out);
    radio_err_t (*init)(void *ctx);
    radio_err_t (*deinit)(void *ctx);

    /* 固定码接收:非阻塞;无数据返回 RADIO_ERR_EMPTY */
    radio_err_t (*learn_poll)(void *ctx, radio_fixed_t *out);

    /* 固定码发射:阻塞到应答/超时;res 可为 NULL */
    radio_err_t (*send)(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res);
    radio_err_t (*long_start)(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res);
    radio_err_t (*long_stop)(void *ctx, const radio_fixed_t *frame, radio_tx_result_t *res);

    /* 原始脉冲(尚未实现的模块返回 UNSUPPORTED) */
    radio_err_t (*raw_start)(void *ctx, const radio_raw_cfg_t *cfg);
    radio_err_t (*raw_stop)(void *ctx);
    radio_err_t (*raw_read)(void *ctx, int32_t *pulses, size_t cap, size_t *out_count);
    radio_err_t (*tx_raw)(void *ctx, const radio_raw_tx_t *tx);
    void (*cancel)(void *ctx);
} radio_ops_t;

typedef struct {
    const radio_ops_t *ops;
    void *ctx;
    radio_info_t info;
    radio_state_t state;
    bool inited;
} radio_t;

void radio_bind(radio_t *r, const radio_ops_t *ops, void *ctx);
const char *radio_err_name(radio_err_t err);
const char *radio_state_name(radio_state_t st);

radio_err_t radio_probe(radio_t *r);
radio_err_t radio_init(radio_t *r);
radio_err_t radio_deinit(radio_t *r);
bool radio_has_cap(const radio_t *r, uint32_t cap);
uint32_t radio_caps(const radio_t *r);

radio_err_t radio_learn_poll(radio_t *r, radio_fixed_t *out);
radio_err_t radio_send(radio_t *r, const radio_fixed_t *frame, radio_tx_result_t *res);
radio_err_t radio_long_start(radio_t *r, const radio_fixed_t *frame, radio_tx_result_t *res);
radio_err_t radio_long_stop(radio_t *r, const radio_fixed_t *frame, radio_tx_result_t *res);

radio_err_t radio_raw_start(radio_t *r, const radio_raw_cfg_t *cfg);
radio_err_t radio_raw_stop(radio_t *r);
radio_err_t radio_raw_read(radio_t *r, int32_t *pulses, size_t cap, size_t *out_count);
radio_err_t radio_tx_raw(radio_t *r, const radio_raw_tx_t *tx);
void radio_cancel(radio_t *r);

#endif /* RFLAB_RADIO_H */
