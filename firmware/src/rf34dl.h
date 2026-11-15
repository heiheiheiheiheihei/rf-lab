/*
 * YS-RF34DL 串口帧编解码 —— 纯 C,不依赖 ESP-IDF,可在主机上单测。
 *
 * 帧长固定 8 字节,头 0xFD、尾 0xDF:
 *
 *   发射     FD <F3|F4> <03|FE|FF> <c0 c1 c2 c3> DF
 *   解码     FD <F3|F4> <03>       <c0 c1 c2 c3> DF
 *   波特率   FD <01..04> <F1> <00 00 00 00> DF
 *   输出模式 FD <01|02> <F2> <00 00 00 00> DF
 *   应答     单字节 F0/F1/F2/F3(不在帧内);
 *            长发期间周期性 0xFE,停止时 0xFF(2026-11-15 实测,见 evidence)
 *
 * 第三个字节在两方向含义不同:发射=持续时间,解码=固定 0x03。
 * 因此结构体里叫 b3,不做语义假设。
 */
#ifndef RFLAB_RF34DL_H
#define RFLAB_RF34DL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF34DL_HEAD 0xFD
#define RF34DL_TAIL 0xDF
#define RF34DL_FRAME_LEN 8
#define RF34DL_CODE_LEN 4

/* 频段字节 */
#define RF34DL_BAND_315 0xF3
#define RF34DL_BAND_433 0xF4

/* 发射持续时间(第三字节):示例与抓包都是 0x03;FE/FE 为长发/停止 */
#define RF34DL_DUR_NORMAL 0x03
#define RF34DL_DUR_LONG 0xFE
#define RF34DL_DUR_STOP 0xFF

/* 解码帧第三字节固定值 */
#define RF34DL_RX_FIXED 0x03

/* 应答字节 */
#define RF34DL_ACK_TX_OK 0xF0    /* 单次发射完成(实测 ~0.85s 才到) */
#define RF34DL_ACK_BAUD_OK 0xF1  /* 波特率已切换 */
#define RF34DL_ACK_SINGLE_OK 0xF2
#define RF34DL_ACK_CONT_OK 0xF3
#define RF34DL_ACK_LONG_ON 0xFE  /* 长发进行中:约 25Hz 周期重复 */
#define RF34DL_ACK_STOPPED 0xFF  /* 长发已停止:停止命令后出现一次 */

typedef struct {
    uint8_t band; /* F3 / F4 */
    uint8_t b3;   /* 发射=持续时间;解码=0x03 */
    uint8_t code[RF34DL_CODE_LEN];
} rf34dl_frame_t;

typedef enum {
    RF34DL_EV_NONE = 0,
    RF34DL_EV_FRAME = 1,
    RF34DL_EV_ACK = 2,
} rf34dl_ev_t;

typedef struct {
    uint8_t buf[RF34DL_FRAME_LEN];
    uint8_t len;
    bool in_frame;
} rf34dl_parse_t;

typedef struct {
    rf34dl_parse_t st;
    rf34dl_frame_t frame;
    uint8_t ack;
    uint32_t dropped; /* 帧校验失败后的重同步次数 */
    uint32_t stray;   /* 帧外既不是 FD 也不是应答的字节数 */
} rf34dl_parser_t;

/* 工具 */
bool rf34dl_band_ok(uint8_t band);
const char *rf34dl_band_name(uint8_t band);
int rf34dl_baud_sel(int baud); /* 4800/9600/57600/115200 -> 1..4,否则 0 */
int rf34dl_sel_baud(int sel);  /* 1..4 -> 波特率,否则 0 */
bool rf34dl_is_ack(uint8_t b);

/* 组帧:成功返回 8,参数非法返回 0(输出缓冲不会被写坏) */
size_t rf34dl_build_tx(uint8_t out[RF34DL_FRAME_LEN], uint8_t band, uint8_t dur,
                       const uint8_t code[RF34DL_CODE_LEN]);
size_t rf34dl_build_baud(uint8_t out[RF34DL_FRAME_LEN], int sel);
size_t rf34dl_build_mode(uint8_t out[RF34DL_FRAME_LEN], bool continuous);

/* 解码:逐字节喂入,返回事件;帧/应答放在 parser 里 */
void rf34dl_parser_init(rf34dl_parser_t *p);
rf34dl_ev_t rf34dl_parser_feed(rf34dl_parser_t *p, uint8_t b);

#endif /* RFLAB_RF34DL_H */
