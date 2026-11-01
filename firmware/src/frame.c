#include "frame.h"

#include <string.h>

enum {
    ST_MAGIC = 0,
    ST_VER,
    ST_TYPE,
    ST_SEQ,
    ST_LEN_LO,
    ST_LEN_HI,
    ST_PAYLOAD,
};

void rflab_rx_init(rflab_rx_t *rx)
{
    memset(rx, 0, sizeof(*rx));
    rx->state = ST_MAGIC;
}

void rflab_rx_reset(rflab_rx_t *rx)
{
    /* 只复位"在途"状态;type/seq/len/got 无意义,一并清零便于调试。 */
    rx->state = ST_MAGIC;
    rx->type  = 0;
    rx->seq   = 0;
    rx->len   = 0;
    rx->got   = 0;
}

bool rflab_rx_in_progress(const rflab_rx_t *rx)
{
    return rx->state != ST_MAGIC;
}

int rflab_rx_feed(rflab_rx_t *rx, uint8_t b, rflab_frame_t *out)
{
    switch (rx->state) {
    case ST_MAGIC:
        /* 重新同步:只在见到 MAGIC 时才进入帧解析。任何噪声/日志文本都被这一条吃掉。 */
        rx->state = (b == RFLAB_MAGIC) ? ST_VER : ST_MAGIC;
        return RFLAB_RX_MORE;

    case ST_VER:
        if (b != RFLAB_VER) {
            rx->state = ST_MAGIC;
            return RFLAB_RX_BAD_VER;
        }
        rx->state = ST_TYPE;
        return RFLAB_RX_MORE;

    case ST_TYPE:
        rx->type = b;
        rx->state = ST_SEQ;
        return RFLAB_RX_MORE;

    case ST_SEQ:
        rx->seq = b;
        rx->state = ST_LEN_LO;
        return RFLAB_RX_MORE;

    case ST_LEN_LO:
        rx->len = b;
        rx->state = ST_LEN_HI;
        return RFLAB_RX_MORE;

    case ST_LEN_HI:
        rx->len |= (uint16_t)((uint16_t)b << 8);
        /* 上限立刻判定:超长帧必须在收载荷之前拒掉,否则一个 65535B 的
         * 声明就能让我们一直停在收载荷状态(拒绝服务)。 */
        if (rx->len > RFLAB_USB_FRAME_MAX) {
            rx->state = ST_MAGIC;
            return RFLAB_RX_TOO_BIG;
        }
        rx->got = 0;
        rx->state = (rx->len == 0) ? ST_MAGIC : ST_PAYLOAD;
        if (rx->len == 0) {
            /* 空载荷也是合法帧(例如 {} 的等价物)。 */
            out->type = rx->type;
            out->seq  = rx->seq;
            out->len  = 0;
            return RFLAB_RX_FRAME;
        }
        return RFLAB_RX_MORE;

    case ST_PAYLOAD:
        out->payload[rx->got++] = b;
        if (rx->got >= rx->len) {
            out->type = rx->type;
            out->seq  = rx->seq;
            out->len  = rx->len;
            rx->state = ST_MAGIC;
            return RFLAB_RX_FRAME;
        }
        return RFLAB_RX_MORE;

    default:
        rx->state = ST_MAGIC;
        return RFLAB_RX_MORE;
    }
}

size_t rflab_frame_encode(uint8_t *buf, size_t cap, uint8_t type, uint8_t seq,
                          const char *json, size_t json_len)
{
    size_t need = RFLAB_HDR_LEN + json_len;

    if (json_len > RFLAB_USB_FRAME_MAX) {
        return 0;
    }
    if (cap < need) {
        return 0; /* 不截断:截断的帧会被下游当成完整消息。 */
    }

    buf[0] = RFLAB_MAGIC;
    buf[1] = RFLAB_VER;
    buf[2] = type;
    buf[3] = seq;
    buf[4] = (uint8_t)(json_len & 0xFF);
    buf[5] = (uint8_t)((json_len >> 8) & 0xFF);

    if (json != NULL && json_len > 0) {
        memcpy(buf + RFLAB_HDR_LEN, json, json_len);
    }
    return need;
}
