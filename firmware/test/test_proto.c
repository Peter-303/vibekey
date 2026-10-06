/**
 * @file test_proto.c
 * @brief 协议编解码测试。前三组向量与 docs/00-技术设计文档.md §3.3 逐字节一致，
 *        且与 host 端 FrameCodecTests 共用。
 */
#include "proto.h"
#include "test_util.h"

#include <string.h>

VK_SUITE(proto)
{
    uint8_t buf[64];
    size_t len = 0u;
    int rc;

    /* --- 文档向量 1：按键 K1 按下，SEQ=1 --- */
    {
        proto_frame_t f;
        static const uint8_t expect[] = {
            0xAA, 0x01, 0x00, 0x01, 0x01, 0x00, 0x31, 0x46, 0x2C
        };
        f.type = PROTO_T_KEY_EVENT;
        f.seq = 1u;
        f.len = 1u;
        f.payload[0] = (uint8_t)'1';
        rc = proto_encode(&f, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        VK_CHECK_EQ_INT(len, 9);
        VK_CHECK_MEM(buf, expect, sizeof(expect));
    }

    /* --- 文档向量 2：心跳 ping，SEQ=1000 --- */
    {
        proto_frame_t f;
        static const uint8_t expect[] = {
            0xAA, 0x00, 0x00, 0x10, 0xE8, 0x03, 0x17, 0xFB
        };
        f.type = PROTO_T_PING;
        f.seq = 1000u;
        f.len = 0u;
        rc = proto_encode(&f, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        VK_CHECK_EQ_INT(len, 8);
        VK_CHECK_MEM(buf, expect, sizeof(expect));
    }

    /* --- 文档向量 3：状态上报，SEQ=7 --- */
    {
        proto_frame_t f;
        static const char payload[] = "WORK_READY,FW=1.0.0,RSSI=-58";
        static const uint8_t crc_expect[] = { 0x68, 0xD0 };
        f.type = PROTO_T_STATUS;
        f.seq = 7u;
        f.len = (uint16_t)(sizeof(payload) - 1u);
        memcpy(f.payload, payload, sizeof(payload) - 1u);
        rc = proto_encode(&f, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        VK_CHECK_EQ_INT(len, 36);
        VK_CHECK_MEM(&buf[len - 2u], crc_expect, 2);
        VK_CHECK_EQ_INT(buf[1], 28);          /* LEN 低字节 */
        VK_CHECK_EQ_INT(buf[2], 0);           /* LEN 高字节 */
        VK_CHECK_MEM(&buf[6], payload, sizeof(payload) - 1u);
    }

    /* --- 编解码往返 --- */
    {
        proto_frame_t in;
        proto_frame_t out;
        size_t consumed = 0u;
        proto_dec_result_t r;

        in.type = PROTO_T_ASR_TEXT;
        in.seq = 65535u;
        in.len = 11u;
        memcpy(in.payload, "hello world", 11u);
        rc = proto_encode(&in, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        r = proto_decode(buf, len, &out, &consumed);
        VK_CHECK_EQ_INT(r, PROTO_DEC_OK);
        VK_CHECK_EQ_INT(consumed, (long)len);
        VK_CHECK_EQ_INT(out.type, PROTO_T_ASR_TEXT);
        VK_CHECK_EQ_INT(out.seq, 65535);
        VK_CHECK_EQ_INT(out.len, 11);
        VK_CHECK_MEM(out.payload, "hello world", 11u);
    }

    /* --- LEN=0 往返 --- */
    {
        proto_frame_t in;
        proto_frame_t out;
        size_t consumed = 0u;
        in.type = PROTO_T_PONG;
        in.seq = 0u;
        in.len = 0u;
        rc = proto_encode(&in, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        VK_CHECK_EQ_INT(proto_decode(buf, len, &out, &consumed), PROTO_DEC_OK);
        VK_CHECK_EQ_INT(out.len, 0);
        VK_CHECK_EQ_INT(out.type, PROTO_T_PONG);
    }

    /* --- 含 NUL 的载荷（WiFi 配置 "SSID\0PASS\0"）--- */
    {
        proto_frame_t in;
        proto_frame_t out;
        size_t consumed = 0u;
        static const uint8_t pl[] = { 'M','y','S','S','I','D', 0x00, 'p','a','s','s', 0x00 };
        in.type = PROTO_T_WIFI_CFG;
        in.seq = 42u;
        in.len = (uint16_t)sizeof(pl);
        memcpy(in.payload, pl, sizeof(pl));
        rc = proto_encode(&in, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        VK_CHECK_EQ_INT(proto_decode(buf, len, &out, &consumed), PROTO_DEC_OK);
        VK_CHECK_EQ_INT(out.len, (long)sizeof(pl));
        VK_CHECK_MEM(out.payload, pl, sizeof(pl));
    }

    /* --- 垃圾前缀：应返回 NEED_MORE 且 consumed=垃圾长度 --- */
    {
        proto_frame_t in;
        proto_frame_t out;
        size_t consumed = 0u;
        uint8_t noisy[32];
        size_t i;
        in.type = PROTO_T_KEY_EVENT;
        in.seq = 5u;
        in.len = 1u;
        in.payload[0] = (uint8_t)'7';
        rc = proto_encode(&in, &noisy[2], sizeof(noisy) - 2u, &len);
        VK_CHECK_EQ_INT(rc, 0);
        noisy[0] = 0x00u;
        noisy[1] = 0x11u;
        VK_CHECK_EQ_INT(proto_decode(noisy, len + 2u, &out, &consumed), PROTO_DEC_NEED_MORE);
        VK_CHECK_EQ_INT(consumed, 2);
        /* 跳过垃圾后应能解出 */
        VK_CHECK_EQ_INT(proto_decode(&noisy[2], len, &out, &consumed), PROTO_DEC_OK);
        VK_CHECK_EQ_INT(out.seq, 5);
        VK_CHECK_MEM(out.payload, "7", 1u);

        /* 整个缓冲区都没有帧头 */
        for (i = 0u; i < 8u; i++) { noisy[i] = 0x00u; }
        VK_CHECK_EQ_INT(proto_decode(noisy, 8u, &out, &consumed), PROTO_DEC_NEED_MORE);
        VK_CHECK_EQ_INT(consumed, 8);
    }

    /* --- 坏 CRC --- */
    {
        proto_frame_t in;
        proto_frame_t out;
        size_t consumed = 0u;
        in.type = PROTO_T_KEY_EVENT;
        in.seq = 1u;
        in.len = 1u;
        in.payload[0] = (uint8_t)'1';
        rc = proto_encode(&in, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        buf[6] ^= 0xFFu;   /* 破坏载荷 */
        VK_CHECK_EQ_INT(proto_decode(buf, len, &out, &consumed), PROTO_DEC_BAD_CRC);
        VK_CHECK_EQ_INT(consumed, 1);
    }

    /* --- 坏 LEN --- */
    {
        proto_frame_t out;
        size_t consumed = 0u;
        static const uint8_t bad[] = { 0xAA, 0xFF, 0x0F, 0x01, 0x00, 0x00 };
        VK_CHECK_EQ_INT(proto_decode(bad, sizeof(bad), &out, &consumed), PROTO_DEC_BAD_LEN);
        VK_CHECK_EQ_INT(consumed, 1);
    }

    /* --- 半包 --- */
    {
        proto_frame_t in;
        proto_frame_t out;
        size_t consumed = 0u;
        in.type = PROTO_T_STATUS;
        in.seq = 7u;
        in.len = 4u;
        memcpy(in.payload, "BOOT", 4u);
        rc = proto_encode(&in, buf, sizeof(buf), &len);
        VK_CHECK_EQ_INT(rc, 0);
        VK_CHECK_EQ_INT(proto_decode(buf, 5u, &out, &consumed), PROTO_DEC_NEED_MORE);
        VK_CHECK_EQ_INT(consumed, 0);
    }

    /* --- 编码器错误码 --- */
    {
        proto_frame_t f;
        f.type = PROTO_T_PING;
        f.seq = 1u;
        f.len = (uint16_t)(PROTO_MAX_PAYLOAD + 1u);
        VK_CHECK_EQ_INT(proto_encode(&f, buf, sizeof(buf), &len), -2);

        f.len = 1u;
        f.payload[0] = 0u;
        VK_CHECK_EQ_INT(proto_encode(&f, buf, 4u, &len), -3);   /* 缓冲不足 */
    }

    /* --- CRC 已知值 --- */
    {
        /* CRC16/CCITT-FALSE("123456789") == 0x29B1（标准校验值） */
        VK_CHECK_EQ_INT(proto_crc16((const uint8_t *)"123456789", 9), 0x29B1);
        VK_CHECK_EQ_INT(proto_crc16(NULL, 4), 0);
    }

    /* --- TYPE 方向判定 --- */
    VK_CHECK_EQ_INT(proto_type_is_downlink(PROTO_T_KEY_EVENT), 0);
    VK_CHECK_EQ_INT(proto_type_is_downlink(PROTO_T_PING), 1);
    VK_CHECK_EQ_INT(proto_type_is_downlink(PROTO_T_ENTER_DFU), 1);
    VK_CHECK_EQ_INT(proto_type_is_downlink(PROTO_T_ACK), 0);

    /* --- 帧长辅助 --- */
    VK_CHECK_EQ_INT((long)proto_frame_size(0u), 8);
    VK_CHECK_EQ_INT((long)proto_frame_size(1024u), 1032);

    /* --- magic word --- */
    VK_CHECK_EQ_INT(PROTO_DFU_MAGIC_LEN, 8);
    VK_CHECK_MEM(proto_dfu_magic, "VIBEFLAS", 8);
}

/* --------------------------- 流式解码器 -------------------------------- */
VK_SUITE(proto_stream)
{
    proto_stream_t s;
    proto_frame_t frames[4];
    size_t cap;
    proto_dec_result_t r;
    uint8_t chunk1[16];
    uint8_t chunk2[16];
    size_t l1 = 0u;
    size_t l2 = 0u;
    proto_frame_t a;
    proto_frame_t b;

    /* 构造两帧：按键 '1'(SEQ=1) 与 按键 '2'(SEQ=2)，故意从中间切断喂入 */
    a.type = PROTO_T_KEY_EVENT; a.seq = 1u; a.len = 1u; a.payload[0] = (uint8_t)'1';
    b.type = PROTO_T_KEY_EVENT; b.seq = 2u; b.len = 1u; b.payload[0] = (uint8_t)'2';
    VK_CHECK_EQ_INT(proto_encode(&a, chunk1, sizeof(chunk1), &l1), 0);   /* 9 字节 */
    VK_CHECK_EQ_INT(proto_encode(&b, chunk2, sizeof(chunk2), &l2), 0);

    proto_stream_init(&s);

    /* 喂前 4 字节（不足一整帧） */
    cap = 4u;
    r = proto_stream_feed(&s, chunk1, 4u, frames, &cap);
    VK_CHECK_EQ_INT(r, PROTO_DEC_NEED_MORE);
    VK_CHECK_EQ_INT(cap, 0);

    /* 喂完第一帧剩余部分 + 第二帧前 3 字节 */
    {
        uint8_t mixed[16];
        memcpy(mixed, &chunk1[4], l1 - 4u);
        memcpy(&mixed[l1 - 4u], chunk2, 3u);
        cap = 4u;
        r = proto_stream_feed(&s, mixed, (l1 - 4u) + 3u, frames, &cap);
        VK_CHECK_EQ_INT(r, PROTO_DEC_OK);
        VK_CHECK_EQ_INT(cap, 1);
        VK_CHECK_EQ_INT(frames[0].seq, 1);
        VK_CHECK_MEM(frames[0].payload, "1", 1u);
    }

    /* 喂完第二帧剩余部分 */
    cap = 4u;
    r = proto_stream_feed(&s, &chunk2[3], l2 - 3u, frames, &cap);
    VK_CHECK_EQ_INT(r, PROTO_DEC_OK);
    VK_CHECK_EQ_INT(cap, 1);
    VK_CHECK_EQ_INT(frames[0].seq, 2);
    VK_CHECK_MEM(frames[0].payload, "2", 1u);
}

/* 垃圾前缀恢复：一次喂入 [垃圾][真帧]，必须当场解出真帧，
 * 不能滞留到下一次 feed（这正是修复掉的 proto_stream bug）。 */
VK_SUITE(proto_stream_garbage)
{
    proto_stream_t s;
    proto_frame_t frames[2];
    size_t cap;
    proto_dec_result_t r;
    uint8_t noisy[3 + 16];
    uint8_t f[16];
    size_t fl = 0u;
    proto_frame_t in;

    in.type = PROTO_T_KEY_EVENT;
    in.seq = 9u;
    in.len = 1u;
    in.payload[0] = (uint8_t)'5';
    VK_CHECK_EQ_INT(proto_encode(&in, f, sizeof(f), &fl), 0);

    proto_stream_init(&s);

    /* 只有一个假帧头 + 一个字节，凑不成帧 -> 应被跳过 */
    noisy[0] = 0x00u;
    noisy[1] = 0xAAu;
    noisy[2] = 0x01u;
    memcpy(&noisy[3], f, fl);

    cap = 2u;
    r = proto_stream_feed(&s, noisy, 3u + fl, frames, &cap);
    VK_CHECK_EQ_INT(r, PROTO_DEC_OK);
    VK_CHECK_EQ_INT(cap, 1);
    VK_CHECK_EQ_INT(frames[0].seq, 9);
    VK_CHECK_EQ_INT(frames[0].payload[0], '5');
}

void test_proto_run(void);

void test_proto_run(void)
{
    vk_suite_name = "proto";
    (void)printf("  [proto] 文档向量 / 往返 / 失步恢复 / 坏帧 / 流式解码\n");
    suite_proto();
    suite_proto_stream();
    suite_proto_stream_garbage();
}
