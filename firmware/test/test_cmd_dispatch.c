/**
 * @file test_cmd_dispatch.c
 * @brief 下行指令分发测试，重点是烧录态唯一入口的四道校验（设计文档 §7.1）。
 */
#include "cmd_dispatch.h"
#include "test_util.h"

#include <string.h>

/* 构造一个下行帧的辅助函数 */
static void mk_frame(proto_frame_t *f, uint8_t type, uint16_t seq,
                     const void *payload, uint16_t len)
{
    f->type = type;
    f->seq = seq;
    f->len = len;
    if ((len > 0u) && (payload != NULL)) {
        memcpy(f->payload, payload, len);
    }
}

/* ------------------------- ASR 配置解析 -------------------------------- */
VK_SUITE(cmd_asr_parse)
{
    vk_asr_cfg_t cfg;
    int rc;
    /* "provider=tencent\0appid=123\0secret_id=sid\0secret_key=skey\0" */
    static const char pl[] =
        "provider=tencent\0appid=1250000001\0"
        "secret_id=AKIDxxxxxxxx\0secret_key=abcdef123456\0"
        "sample_rate=16000\0format=pcm\0channels=1\0voice_key=7\0";

    rc = vk_cmd_parse_asr((const uint8_t *)pl, (uint16_t)(sizeof(pl) - 1u), &cfg);
    VK_CHECK_EQ_INT(rc, 0);
    VK_CHECK_EQ_STR(cfg.provider, "tencent");
    VK_CHECK_EQ_STR(cfg.appid, "1250000001");
    VK_CHECK_EQ_STR(cfg.secret_id, "AKIDxxxxxxxx");
    VK_CHECK_EQ_STR(cfg.secret_key, "abcdef123456");
    VK_CHECK_EQ_INT(cfg.sample_rate, 16000);
    VK_CHECK_EQ_STR(cfg.format, "pcm");
    VK_CHECK_EQ_INT(cfg.channels, 1);
    VK_CHECK_EQ_INT(cfg.voice_key, '7');

    /* 缺必填键 */
    {
        static const char bad[] = "provider=tencent\0appid=x\0";
        VK_CHECK(vk_cmd_parse_asr((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                  &cfg) != 0);
    }
    /* 未以 NUL 结尾 */
    {
        static const char bad[] = "provider=tencent\0appid=x";
        VK_CHECK(vk_cmd_parse_asr((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                  &cfg) != 0);
    }
    /* 非法采样率 */
    {
        static const char bad[] =
            "provider=t\0appid=a\0secret_id=b\0secret_key=c\0sample_rate=12345\0";
        VK_CHECK(vk_cmd_parse_asr((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                  &cfg) != 0);
    }
    /* 键值对缺 '=' */
    {
        static const char bad[] =
            "provider\0appid=a\0secret_id=b\0secret_key=c\0";
        VK_CHECK(vk_cmd_parse_asr((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                  &cfg) != 0);
    }
    /* 未知键应被忽略（前向兼容） */
    {
        static const char ok[] =
            "provider=t\0appid=a\0secret_id=b\0secret_key=c\0future_thing=zzz\0";
        VK_CHECK_EQ_INT(vk_cmd_parse_asr((const uint8_t *)ok,
                                         (uint16_t)(sizeof(ok) - 1u), &cfg), 0);
        VK_CHECK_EQ_STR(cfg.provider, "t");
    }
    /* 空参数 */
    VK_CHECK(vk_cmd_parse_asr(NULL, 0, &cfg) != 0);
}

VK_SUITE(cmd_asr_default)
{
    vk_asr_cfg_t cfg;
    vk_asr_cfg_default(&cfg);
    VK_CHECK_EQ_STR(cfg.provider, "tencent");
    VK_CHECK_EQ_INT(cfg.sample_rate, 16000);
    VK_CHECK_EQ_STR(cfg.format, "pcm");
    VK_CHECK_EQ_INT(cfg.channels, 1);
    VK_CHECK_EQ_INT(cfg.voice_key, '7');
}

/* --------------------------- WiFi 配置解析 ----------------------------- */
VK_SUITE(cmd_wifi_parse)
{
    char ssid[33];
    char pass[65];
    /* "MyHome\0password123\0" */
    static const char pl[] = "MyHome\0password123\0";

    VK_CHECK_EQ_INT(vk_cmd_parse_wifi((const uint8_t *)pl, (uint16_t)(sizeof(pl) - 1u),
                                      ssid, sizeof(ssid), pass, sizeof(pass)), 0);
    VK_CHECK_EQ_STR(ssid, "MyHome");
    VK_CHECK_EQ_STR(pass, "password123");

    /* 密码太短（<8） */
    {
        static const char bad[] = "MyHome\0short\0";
        VK_CHECK(vk_cmd_parse_wifi((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                   ssid, sizeof(ssid), pass, sizeof(pass)) != 0);
    }
    /* 缺第一个 NUL */
    {
        static const char bad[] = "MyHomepassword123\0";
        VK_CHECK(vk_cmd_parse_wifi((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                   ssid, sizeof(ssid), pass, sizeof(pass)) != 0);
    }
    /* SSID 含逗号（会破坏状态字符串） */
    {
        static const char bad[] = "My,Home\0password123\0";
        VK_CHECK(vk_cmd_parse_wifi((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                   ssid, sizeof(ssid), pass, sizeof(pass)) != 0);
    }
    /* SSID 含等号 */
    {
        static const char bad[] = "My=Home\0password123\0";
        VK_CHECK(vk_cmd_parse_wifi((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                   ssid, sizeof(ssid), pass, sizeof(pass)) != 0);
    }
    /* 结尾缺 NUL */
    {
        static const char bad[] = "MyHome\0password123";
        VK_CHECK(vk_cmd_parse_wifi((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                   ssid, sizeof(ssid), pass, sizeof(pass)) != 0);
    }
    /* 空 SSID */
    {
        static const char bad[] = "\0password123\0";
        VK_CHECK(vk_cmd_parse_wifi((const uint8_t *)bad, (uint16_t)(sizeof(bad) - 1u),
                                   ssid, sizeof(ssid), pass, sizeof(pass)) != 0);
    }
}

/* ------------- 烧录态唯一入口：四道校验逐条验证 ------------------------- */
VK_SUITE(cmd_dfu_guards)
{
    vk_cmd_dispatch_t d;
    proto_frame_t f;
    uint8_t ack[32];
    size_t alen = 0u;
    vk_cmd_action_t act;

    vk_cmd_init(&d, 5000u);
    vk_cmd_set_state(&d, VK_STATE_WORK_READY);

    /* 1) USB + 正确 magic -> 放行 */
    mk_frame(&f, PROTO_T_ENTER_DFU, 1u, proto_dfu_magic, PROTO_DFU_MAGIC_LEN);
    alen = 0u;
    act = vk_cmd_handle(&d, &f, VK_CH_USB, 1000u, ack, sizeof(ack), &alen);
    VK_CHECK_EQ_INT(act, VK_CMD_ACT_ENTER_DFU);
    VK_CHECK_EQ_INT(alen, 1);
    VK_CHECK_EQ_INT(ack[0], PROTO_ACK_OK);

    /* 2) WiFi 通道来的同一指令 -> 拒绝，原因 CH */
    vk_cmd_init(&d, 5000u);
    alen = 0u;
    act = vk_cmd_handle(&d, &f, VK_CH_WIFI, 1000u, ack, sizeof(ack), &alen);
    VK_CHECK_EQ_INT(act, VK_CMD_ACT_NONE);
    VK_CHECK_EQ_INT(alen, 3);              /* 0x01 + "CH" */
    VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
    VK_CHECK_MEM(&ack[1], "CH", 2u);

    /* 3) magic 错误 -> 拒绝，原因 MAGIC */
    {
        uint8_t bad_magic[PROTO_DFU_MAGIC_LEN];
        memcpy(bad_magic, proto_dfu_magic, PROTO_DFU_MAGIC_LEN);
        bad_magic[3] ^= 0xFFu;
        mk_frame(&f, PROTO_T_ENTER_DFU, 2u, bad_magic, PROTO_DFU_MAGIC_LEN);
        vk_cmd_init(&d, 5000u);
        alen = 0u;
        act = vk_cmd_handle(&d, &f, VK_CH_USB, 1000u, ack, sizeof(ack), &alen);
        VK_CHECK_EQ_INT(act, VK_CMD_ACT_NONE);
        VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
        VK_CHECK_MEM(&ack[1], "MAGIC", 5u);
    }

    /* 4) 长度不是 8 -> 拒绝，原因 LEN */
    {
        mk_frame(&f, PROTO_T_ENTER_DFU, 3u, proto_dfu_magic, 4u);
        vk_cmd_init(&d, 5000u);
        alen = 0u;
        act = vk_cmd_handle(&d, &f, VK_CH_USB, 1000u, ack, sizeof(ack), &alen);
        VK_CHECK_EQ_INT(act, VK_CMD_ACT_NONE);
        VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
        VK_CHECK_MEM(&ack[1], "LEN", 3u);
    }

    /* 5) ERROR 态 -> 拒绝，原因 STATE */
    {
        mk_frame(&f, PROTO_T_ENTER_DFU, 4u, proto_dfu_magic, PROTO_DFU_MAGIC_LEN);
        vk_cmd_init(&d, 5000u);
        vk_cmd_set_state(&d, VK_STATE_ERROR);
        alen = 0u;
        act = vk_cmd_handle(&d, &f, VK_CH_USB, 1000u, ack, sizeof(ack), &alen);
        VK_CHECK_EQ_INT(act, VK_CMD_ACT_NONE);
        VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
        VK_CHECK_MEM(&ack[1], "STATE", 5u);
    }
}

VK_SUITE(cmd_dfu_replay)
{
    vk_cmd_dispatch_t d;
    proto_frame_t f;
    uint8_t ack[32];
    size_t alen = 0u;
    vk_cmd_action_t act;

    vk_cmd_init(&d, 5000u);
    vk_cmd_set_state(&d, VK_STATE_WORK_READY);
    mk_frame(&f, PROTO_T_ENTER_DFU, 1u, proto_dfu_magic, PROTO_DFU_MAGIC_LEN);

    /* 第一次接受 */
    act = vk_cmd_handle(&d, &f, VK_CH_USB, 1000u, ack, sizeof(ack), &alen);
    VK_CHECK_EQ_INT(act, VK_CMD_ACT_ENTER_DFU);

    /* 窗口内重放：静默忽略，不回 ACK */
    alen = 99u;
    act = vk_cmd_handle(&d, &f, VK_CH_USB, 2000u, ack, sizeof(ack), &alen);
    VK_CHECK_EQ_INT(act, VK_CMD_ACT_NONE);
    VK_CHECK_EQ_INT(alen, 0);

    /* 窗口外重放：再次放行（设备此时已重启，此路径主要覆盖异常重启场景） */
    alen = 0u;
    act = vk_cmd_handle(&d, &f, VK_CH_USB, 10000u, ack, sizeof(ack), &alen);
    VK_CHECK_EQ_INT(act, VK_CMD_ACT_ENTER_DFU);
}

/* --------------------------- 其他下行指令 ------------------------------ */
VK_SUITE(cmd_others)
{
    vk_cmd_dispatch_t d;
    proto_frame_t f;
    uint8_t ack[32];
    size_t alen = 0u;
    static const char wifi[] = "MyHome\0password123\0";
    static const char asr[] =
        "provider=tencent\0appid=a\0secret_id=b\0secret_key=c\0";

    vk_cmd_init(&d, 0u);                  /* 0 -> 默认窗口 */
    VK_CHECK_EQ_INT(d.dfu_window_ms, 5000);

    /* WiFi 配置 */
    mk_frame(&f, PROTO_T_WIFI_CFG, 1u, wifi, (uint16_t)(sizeof(wifi) - 1u));
    alen = 0u;
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_WIFI_SAVE);
    VK_CHECK_EQ_STR(d.wifi_ssid, "MyHome");
    VK_CHECK_EQ_STR(d.wifi_pass, "password123");
    VK_CHECK_EQ_INT(ack[0], PROTO_ACK_OK);

    /* WiFi 配置格式错误 */
    mk_frame(&f, PROTO_T_WIFI_CFG, 2u, "bad", 3u);
    alen = 0u;
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_NONE);
    VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
    VK_CHECK_MEM(&ack[1], "FMT", 3u);

    /* ASR 配置 */
    mk_frame(&f, PROTO_T_ASR_CFG, 3u, asr, (uint16_t)(sizeof(asr) - 1u));
    alen = 0u;
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_ASR_SAVE);
    VK_CHECK_EQ_STR(d.asr_cfg.provider, "tencent");

    /* 语音启停 */
    {
        uint8_t p = (uint8_t)PROTO_VOICE_START;
        mk_frame(&f, PROTO_T_VOICE_CTL, 4u, &p, 1u);
        VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                        VK_CMD_ACT_VOICE_START);
        p = (uint8_t)PROTO_VOICE_STOP;
        mk_frame(&f, PROTO_T_VOICE_CTL, 5u, &p, 1u);
        VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                        VK_CMD_ACT_VOICE_STOP);
    }

    /* 语音载荷长度非法 */
    mk_frame(&f, PROTO_T_VOICE_CTL, 6u, "xx", 2u);
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_NONE);
    VK_CHECK_MEM(&ack[1], "LEN", 3u);

    /* 状态请求 */
    mk_frame(&f, PROTO_T_STATUS_REQ, 7u, NULL, 0u);
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_STATUS_REQ);

    /* ping：回 pong，不再额外回 ACK（pong 本身就是心跳应答） */
    mk_frame(&f, PROTO_T_PING, 8u, NULL, 0u);
    alen = 0u;
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_PONG);
    VK_CHECK_EQ_INT(alen, 0);   /* silent：不产生 ACK 帧 */
    mk_frame(&f, PROTO_T_VER, 9u, "VER=1", 5u);
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_NONE);

    /* 未知类型 -> NACK TYPE */
    mk_frame(&f, 0x55u, 10u, NULL, 0u);
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_NONE);
    VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
    VK_CHECK_MEM(&ack[1], "TYPE", 4u);

    /* 上行业务帧不应被当作指令处理 */
    mk_frame(&f, PROTO_T_KEY_EVENT, 11u, "1", 1u);
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, ack, sizeof(ack), &alen),
                    VK_CMD_ACT_NONE);
    VK_CHECK_EQ_INT(ack[0], PROTO_ACK_NACK);
    VK_CHECK_MEM(&ack[1], "TYPE", 4u);
}

VK_SUITE(cmd_bad_args)
{
    vk_cmd_dispatch_t d;
    proto_frame_t f;
    vk_cmd_init(&d, 0u);
    mk_frame(&f, PROTO_T_PING, 1u, NULL, 0u);
    VK_CHECK_EQ_INT(vk_cmd_handle(NULL, &f, VK_CH_USB, 0u, NULL, 0u, NULL),
                    VK_CMD_ACT_NONE);
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, NULL, VK_CH_USB, 0u, NULL, 0u, NULL),
                    VK_CMD_ACT_NONE);
    /* ack 缓冲为 NULL 时不得崩溃（f 是 PING 帧，返回 PONG 但 silent 不回 ACK） */
    VK_CHECK_EQ_INT(vk_cmd_handle(&d, &f, VK_CH_USB, 0u, NULL, 0u, NULL),
                    VK_CMD_ACT_PONG);
}

void test_cmd_dispatch_run(void)
{
    vk_suite_name = "cmd_dispatch";
    (void)printf("  [cmd_dispatch] 烧录入口四道校验 / 重放 / 配置解析\n");
    suite_cmd_asr_parse();
    suite_cmd_asr_default();
    suite_cmd_wifi_parse();
    suite_cmd_dfu_guards();
    suite_cmd_dfu_replay();
    suite_cmd_others();
    suite_cmd_bad_args();
}
