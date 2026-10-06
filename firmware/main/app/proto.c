/**
 * @file proto.c
 * @brief 协议编解码实现。冻结依据：docs/00-技术设计文档.md §3
 */
#include "proto.h"

#include <string.h>
#include <stddef.h>   /* NULL */

/* "VIBEFLAS" */
const uint8_t proto_dfu_magic[PROTO_DFU_MAGIC_LEN] = {
    0x56u, 0x49u, 0x42u, 0x45u, 0x46u, 0x4Cu, 0x41u, 0x53u
};

uint16_t proto_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    unsigned bit;

    if (data == NULL) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

int proto_type_is_downlink(uint8_t type)
{
    return ((type >= 0x10u) && (type <= 0x1Fu)) ? 1 : 0;
}

size_t proto_frame_size(uint16_t payload_len)
{
    return (size_t)PROTO_HEADER_LEN + (size_t)payload_len + (size_t)PROTO_CRC_LEN;
}

int proto_encode(const proto_frame_t *f, uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t total;
    uint16_t crc;
    uint16_t i;

    if ((f == NULL) || (out == NULL) || (out_len == NULL)) {
        return -1;
    }
    if (f->len > PROTO_MAX_PAYLOAD) {
        return -2;
    }
    total = proto_frame_size(f->len);
    if (out_cap < total) {
        return -3;
    }

    out[0] = (uint8_t)PROTO_SOF;
    out[1] = (uint8_t)(f->len & 0x00FFu);
    out[2] = (uint8_t)((f->len >> 8) & 0x00FFu);
    out[3] = f->type;
    out[4] = (uint8_t)(f->seq & 0x00FFu);
    out[5] = (uint8_t)((f->seq >> 8) & 0x00FFu);
    for (i = 0u; i < f->len; i++) {
        out[PROTO_HEADER_LEN + i] = f->payload[i];
    }

    /* CRC 覆盖 LEN+TYPE+SEQ+PAYLOAD，即 out[1] .. out[1 + (5+LEN) - 1] */
    crc = proto_crc16(&out[1], (size_t)PROTO_HEADER_LEN - 1u + (size_t)f->len);
    out[total - 2u] = (uint8_t)(crc & 0x00FFu);
    out[total - 1u] = (uint8_t)((crc >> 8) & 0x00FFu);

    *out_len = total;
    return 0;
}

proto_dec_result_t proto_decode(const uint8_t *buf, size_t avail,
                                proto_frame_t *out, size_t *consumed)
{
    size_t sof = 0u;
    size_t total;
    uint16_t len;
    uint16_t want;
    uint16_t got;
    uint16_t i;

    if ((buf == NULL) || (out == NULL) || (consumed == NULL)) {
        return PROTO_DEC_BAD_ARG;
    }
    *consumed = 0u;

    /* 1. 定位帧头 */
    while ((sof < avail) && (buf[sof] != (uint8_t)PROTO_SOF)) {
        sof++;
    }
    if (sof >= avail) {
        /* 整个缓冲区都没有帧头，可以全部丢弃 */
        *consumed = avail;
        return PROTO_DEC_NEED_MORE;
    }
    if (sof > 0u) {
        /* 前面是垃圾，先让调用方跳过 */
        *consumed = sof;
        return PROTO_DEC_NEED_MORE;
    }

    /* 2. 帧头是否完整 */
    if (avail < (size_t)PROTO_HEADER_LEN) {
        return PROTO_DEC_NEED_MORE;
    }

    /* 3. LEN 合法性 */
    len = (uint16_t)((uint16_t)buf[1] | (uint16_t)((uint16_t)buf[2] << 8));
    if (len > PROTO_MAX_PAYLOAD) {
        *consumed = 1u;
        return PROTO_DEC_BAD_LEN;
    }

    /* 4. 整帧是否到齐 */
    total = proto_frame_size(len);
    if (avail < total) {
        return PROTO_DEC_NEED_MORE;
    }

    /* 5. CRC 校验 */
    want = (uint16_t)((uint16_t)buf[total - 2u] | (uint16_t)((uint16_t)buf[total - 1u] << 8));
    got  = proto_crc16(&buf[1], total - 3u);   /* LEN+TYPE+SEQ+PAYLOAD = 5+LEN */
    if (want != got) {
        *consumed = 1u;
        return PROTO_DEC_BAD_CRC;
    }

    out->type = buf[3];
    out->seq  = (uint16_t)((uint16_t)buf[4] | (uint16_t)((uint16_t)buf[5] << 8));
    out->len  = len;
    for (i = 0u; i < len; i++) {
        out->payload[i] = buf[PROTO_HEADER_LEN + i];
    }
    *consumed = total;
    return PROTO_DEC_OK;
}

void proto_stream_init(proto_stream_t *s)
{
    if (s == NULL) {
        return;
    }
    s->fill = 0u;
}

proto_dec_result_t proto_stream_feed(proto_stream_t *s, const uint8_t *data, size_t len,
                                     proto_frame_t *out, size_t *inout_cap)
{
    proto_dec_result_t last = PROTO_DEC_NEED_MORE;
    size_t cap;
    size_t produced = 0u;
    size_t off = 0u;

    if ((s == NULL) || (out == NULL) || (inout_cap == NULL)) {
        return PROTO_DEC_BAD_ARG;
    }
    cap = *inout_cap;
    if (cap == 0u) {
        return PROTO_DEC_BAD_ARG;
    }

    while (off < len) {
        size_t room = sizeof(s->buf) - s->fill;
        size_t take = (len - off < room) ? (len - off) : room;

        if (take == 0u) {
            /* 缓冲已满仍无法成帧：丢弃最旧一个字节以维持同步 */
            memmove(s->buf, &s->buf[1], s->fill - 1u);
            s->fill -= 1u;
            continue;
        }
        memcpy(&s->buf[s->fill], &data[off], take);
        s->fill += take;
        off += take;

        /* 尽量多解出几帧 */
        for (;;) {
            proto_frame_t tmp;
            size_t consumed = 0u;
            proto_dec_result_t r = proto_decode(s->buf, s->fill, &tmp, &consumed);

            if (r == PROTO_DEC_OK) {
                if (produced >= cap) {
                    /* 输出已满：帧留在缓冲里，等下次 feed 再取，绝不丢弃 */
                    break;
                }
                out[produced] = tmp;
                produced++;
                memmove(s->buf, &s->buf[consumed], s->fill - consumed);
                s->fill -= consumed;
                last = PROTO_DEC_OK;
                continue;
            }
            if ((r == PROTO_DEC_BAD_CRC) || (r == PROTO_DEC_BAD_LEN)) {
                /* 跳过坏帧头，继续找下一帧 */
                memmove(s->buf, &s->buf[consumed], s->fill - consumed);
                s->fill -= consumed;
                last = r;
                continue;
            }
            if (r == PROTO_DEC_NEED_MORE) {
                if (consumed > 0u) {
                    /* 丢弃垃圾前缀后必须继续解帧：否则紧跟其后的真帧会滞留缓冲，
                     * 直到下一次 feed 才解析（表现为按键事件被延迟/丢失）。 */
                    memmove(s->buf, &s->buf[consumed], s->fill - consumed);
                    s->fill -= consumed;
                    continue;
                }
                break;   /* consumed==0：确实在等更多数据 */
            }
            last = r;
            break;
        }
    }

    *inout_cap = produced;
    return (produced > 0u) ? PROTO_DEC_OK : last;
}
