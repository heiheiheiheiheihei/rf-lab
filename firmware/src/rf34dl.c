#include "rf34dl.h"

#include <string.h>

bool rf34dl_band_ok(uint8_t band)
{
    return band == RF34DL_BAND_315 || band == RF34DL_BAND_433;
}

const char *rf34dl_band_name(uint8_t band)
{
    switch (band) {
    case RF34DL_BAND_315:
        return "315M";
    case RF34DL_BAND_433:
        return "433M";
    default:
        return "???";
    }
}

int rf34dl_baud_sel(int baud)
{
    switch (baud) {
    case 4800:
        return 1;
    case 9600:
        return 2;
    case 57600:
        return 3;
    case 115200:
        return 4;
    default:
        return 0;
    }
}

int rf34dl_sel_baud(int sel)
{
    switch (sel) {
    case 1:
        return 4800;
    case 2:
        return 9600;
    case 3:
        return 57600;
    case 4:
        return 115200;
    default:
        return 0;
    }
}

bool rf34dl_is_ack(uint8_t b)
{
    return b >= RF34DL_ACK_TX_OK && b <= RF34DL_ACK_CONT_OK;
}

size_t rf34dl_build_tx(uint8_t out[RF34DL_FRAME_LEN], uint8_t band, uint8_t dur,
                       const uint8_t code[RF34DL_CODE_LEN])
{
    if (!rf34dl_band_ok(band) || code == NULL) {
        return 0;
    }
    out[0] = RF34DL_HEAD;
    out[1] = band;
    out[2] = dur;
    memcpy(&out[3], code, RF34DL_CODE_LEN);
    out[7] = RF34DL_TAIL;
    return RF34DL_FRAME_LEN;
}

size_t rf34dl_build_baud(uint8_t out[RF34DL_FRAME_LEN], int sel)
{
    if (sel < 1 || sel > 4) {
        return 0;
    }
    out[0] = RF34DL_HEAD;
    out[1] = (uint8_t)sel;
    out[2] = 0xF1;
    memset(&out[3], 0, 4);
    out[7] = RF34DL_TAIL;
    return RF34DL_FRAME_LEN;
}

size_t rf34dl_build_mode(uint8_t out[RF34DL_FRAME_LEN], bool continuous)
{
    out[0] = RF34DL_HEAD;
    out[1] = continuous ? 0x02 : 0x01;
    out[2] = 0xF2;
    memset(&out[3], 0, 4);
    out[7] = RF34DL_TAIL;
    return RF34DL_FRAME_LEN;
}

void rf34dl_parser_init(rf34dl_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

static bool frame_valid(const uint8_t *b)
{
    return b[0] == RF34DL_HEAD && b[7] == RF34DL_TAIL && rf34dl_band_ok(b[1]);
}

rf34dl_ev_t rf34dl_parser_feed(rf34dl_parser_t *p, uint8_t b)
{
    if (!p->st.in_frame) {
        if (b == RF34DL_HEAD) {
            p->st.in_frame = true;
            p->st.len = 0;
            p->st.buf[p->st.len++] = b;
            return RF34DL_EV_NONE;
        }
        if (rf34dl_is_ack(b)) {
            p->ack = b;
            return RF34DL_EV_ACK;
        }
        p->stray++;
        return RF34DL_EV_NONE;
    }

    p->st.buf[p->st.len++] = b;

    /*
     * 帧满 8 字节就校验一次;不合法则丢首字节、左移继续找下一帧。
     * 协议没有校验和,只能靠 头/尾/频段 三个条件,截断帧靠这个重同步兜住。
     */
    while (p->st.len == RF34DL_FRAME_LEN) {
        if (frame_valid(p->st.buf)) {
            p->frame.band = p->st.buf[1];
            p->frame.b3 = p->st.buf[2];
            memcpy(p->frame.code, &p->st.buf[3], RF34DL_CODE_LEN);
            p->st.in_frame = false;
            p->st.len = 0;
            return RF34DL_EV_FRAME;
        }
        p->dropped++;
        memmove(p->st.buf, p->st.buf + 1, RF34DL_FRAME_LEN - 1);
        p->st.len = RF34DL_FRAME_LEN - 1;
    }
    return RF34DL_EV_NONE;
}
