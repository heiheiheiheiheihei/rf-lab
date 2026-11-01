/*
 * frame.h — RF-Lab 帧编解码(契约 §2)
 *
 * 唯一权威:docs/04-protocol.md §2
 *
 *   0        1        2        3        4        5
 *   +--------+--------+--------+--------+--------+--------...
 *   | MAGIC  | VER    | TYPE   | SEQ    | LEN(2B) | PAYLOAD
 *   | 0x52   | 0x01   | u8     | u8     | u16 LE  | LEN bytes
 *   +--------+--------+--------+--------+--------+--------...
 *
 * ⚠️ 帧头**没有 CRC 字段**。不要"觉得应该有"就加一个 —— 加了 App 侧会整体错位。
 *    MAGIC 只用于重新同步,不做完整性校验;完整性由承载层(USB CDC)保证。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFLAB_MAGIC        0x52u  /* 'R' */
#define RFLAB_VER          0x01u
#define RFLAB_HDR_LEN      6u

/* USB 链路单帧 LEN 上限(契约 §2.2.2 / §4 limits.usb_frame_max)。
 * ⚠️ 这是本固件**对外承诺**的值,CAPS.limits.usb_frame_max 必须报同一个数,
 *    否则 App 会按一个设备无法兑现的尺寸发帧。 */
#define RFLAB_USB_FRAME_MAX 16384u

/* 消息类型(契约 §3)。最高位 0x80 = 二进制数据帧。
 * 本批只实现 0x01/0x02/0x03 与其应答;其余待实现。 */
enum {
    MSG_HELLO     = 0x01, /* App→设备 */
    MSG_GET_CAPS  = 0x02, /* App→设备 */
    MSG_PING      = 0x03, /* 双向     */
    MSG_WARN      = 0x7E, /* 设备→App */
    MSG_ERROR     = 0x7F, /* 设备→App */
    MSG_HELLO_ACK = 0x81, /* 设备→App */
    MSG_CAPS      = 0x82, /* 设备→App */
    MSG_PONG      = 0x83, /* 设备→App */
};

typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint8_t  payload[RFLAB_USB_FRAME_MAX];
} rflab_frame_t;

/* 接收状态机:逐字节喂入,任何时刻都能从任意字节重新同步。
 * ⚠️ 上限检查在读到 LEN 时立刻做 —— 不能先收 65535B 再判断超长。 */
typedef struct {
    uint8_t  state;
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint16_t got;
} rflab_rx_t;

#define RFLAB_RX_OK       0
#define RFLAB_RX_MORE     0
#define RFLAB_RX_FRAME    1
#define RFLAB_RX_BAD_VER  (-1) /* VER 不匹配:已重同步,上层回 ERROR{version_mismatch} */
#define RFLAB_RX_TOO_BIG  (-2) /* LEN 超 usb_frame_max:上层回 ERROR{frame_too_large} */

void   rflab_rx_init(rflab_rx_t *rx);
int    rflab_rx_feed(rflab_rx_t *rx, uint8_t byte, rflab_frame_t *out);

/* 复位接收状态机(回到等 MAGIC)。
 * 用途(契约 §2 未定义字节间超时,这是实现层的重同步兜底):
 *   一帧载荷在传输中丢了尾部时,解析器会停在 ST_PAYLOAD 把后续新帧当载荷吃掉;
 *   接收层在"无字节超过 200ms"时调用本函数,立刻回到干净的等 MAGIC 状态。 */
void rflab_rx_reset(rflab_rx_t *rx);

/* 是否处于帧中途(已见到 MAGIC、尚未收完一帧)。 */
bool rflab_rx_in_progress(const rflab_rx_t *rx);

/* 编码一帧到 buf。返回整帧长度;cap 不足返回 0(不截断)。
 * payload 为 NULL 时按空载荷编码(LEN=0)。 */
size_t rflab_frame_encode(uint8_t *buf, size_t cap, uint8_t type, uint8_t seq,
                          const char *json, size_t json_len);
