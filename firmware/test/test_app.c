/**
 * @file test_app.c
 * @brief 应用层集成测试：用 stub_port 驱动完整 app，验证端到端行为。
 *
 * 这是"不等硬件就能验证大部分 M1/M3 逻辑"的直接体现。
 */
#include "app.h"
#include "test_util.h"

#include <string.h>

/* 来自 stub_port.c */
extern uint8_t  vk_stub_raw[VK_RAW_INPUT_COUNT];
extern char     vk_stub_sta_ip[16];
extern int      vk_stub_rssi;
extern unsigned vk_stub_frames_sent;
extern uint8_t  vk_stub_last_type;
extern uint16_t vk_stub_last_seq;
extern uint16_t vk_stub_last_len;
extern uint8_t  vk_stub_last_payload[PROTO_MAX_PAYLOAD];
extern unsigned vk_stub_reboots;
extern unsigned vk_stub_uart1_lines;
extern uint8_t  vk_stub_led_blue;
extern uint8_t  vk_stub_led_red;
void vk_stub_reset(void);
void vk_stub_advance_ms(uint32_t delta);

/** 构造一个下行帧的编码字节，喂给 app */
static size_t build_downlink(uint8_t type, uint16_t seq, const void *payload,
                             uint16_t len, uint8_t *out, size_t cap)
{
    proto_frame_t f;
    size_t n = 0u;
    f.type = type;
    f.seq = seq;
    f.len = len;
    if ((len > 0u) && (payload != NULL)) {
        memcpy(f.payload, payload, len);
    }
    if (proto_encode(&f, out, cap, &n) != 0) {
        return 0u;
    }
    return n;
}

/* --------------------- 启动：无凭据 -> AP --------------------- */
VK_SUITE(app_boot_to_ap)
{
    vk_app_t app;
    uint8_t buf[64];
    size_t n;

    vk_stub_reset();
    vk_app_init(&app);

    /* 空 NVS -> 直接 AP 态，红灯蓝灯交替（在某一相必有一盏亮） */
    VK_CHECK_EQ_INT(app.state, VK_STATE_AP_START);
    VK_CHECK_EQ_STR(app.net.ap_ssid, "VibeKey-");

    /* BOOT 帧已通过 UART1 上报 */
    VK_CHECK(vk_stub_uart1_lines >= 1u);

    /* AP 态下 LED 必然有一盏亮（交替闪烁的某一相） */
    VK_CHECK((vk_stub_led_blue != 0u) || (vk_stub_led_red != 0u));

    /* 上位机下发正确 WiFi 配置 -> 写 NVS 并重启 */
    {
        static const char pl[] = "MyHome\0password123\0";
        n = build_downlink(PROTO_T_WIFI_CFG, 1u, pl, (uint16_t)(sizeof(pl) - 1u),
                           buf, sizeof(buf));
        VK_CHECK(n > 0u);
        vk_app_on_usb_bytes(&app, buf, (uint32_t)n);
        VK_CHECK_EQ_INT(vk_stub_reboots, 1u);
    }
}

/* --------------------- 启动：有凭据 -> STA --------------------- */
VK_SUITE(app_boot_to_sta)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);

    /* stub 的 nvs_get_str 返回 NOENT，所以走 AP；这里手动注入凭据验证 STA 路径 */
    vk_net_on_creds(&app.net, 1u, "MyHome", "password123", 0u);
    strcpy(vk_stub_sta_ip, "192.168.1.23");
    vk_stub_rssi = -47;

    (void)vk_app_poll(&app);
    VK_CHECK_EQ_INT(vk_net_state(&app.net), VK_NET_ST_CONNECTED);
    VK_CHECK_EQ_INT(app.state, VK_STATE_WORK_READY);

    /* 工作态：蓝灯常亮 */
    VK_CHECK_EQ_INT(vk_stub_led_blue, 1);
    VK_CHECK_EQ_INT(vk_stub_led_red, 0);

    /* 状态上报里应带 IP 与 RSSI */
    VK_CHECK_EQ_STR(app.status.ip, "192.168.1.23");
    VK_CHECK_EQ_INT(app.status.rssi, -47);
}

/* --------------------- 按键 -> 0x01 帧 --------------------- */
VK_SUITE(app_key_event)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);

    /* init 结束时留了一帧待发状态，先冲掉再计数 */
    vk_stub_advance_ms(10u);
    (void)vk_app_poll(&app);
    vk_stub_frames_sent = 0u;

    /* 按下 K1 并稳定超过 15ms */
    vk_stub_raw[0] = 1u;
    vk_stub_advance_ms(100u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u);
    (void)vk_app_poll(&app);

    /* 应发出恰好一个 0x01 帧，载荷 '1' */
    VK_CHECK_EQ_INT(vk_stub_frames_sent, 1u);
    VK_CHECK_EQ_INT(vk_stub_last_type, PROTO_T_KEY_EVENT);
    VK_CHECK_EQ_INT(vk_stub_last_len, 1);
    VK_CHECK_EQ_INT(vk_stub_last_payload[0], '1');

    /* 继续按住不应连发：这 200ms 内一帧都不该发（心跳是 1s，尚未触发） */
    vk_stub_frames_sent = 0u;
    {
        int i;
        for (i = 0; i < 40; i++) {
            vk_stub_advance_ms(5u);
            (void)vk_app_poll(&app);
        }
    }
    VK_CHECK_EQ_INT(vk_stub_frames_sent, 0u);
}

/* --------------------- DFU：USB 放行 / WiFi 拒绝 --------------------- */
VK_SUITE(app_dfu_channel_rule)
{
    vk_app_t app;
    uint8_t buf[64];
    size_t n;
    static const uint8_t magic[] = "VIBEFLAS";   /* 含结尾 NUL，只取前 8 字节 */

    /* USB -> 放行并重启 */
    vk_stub_reset();
    vk_app_init(&app);
    n = build_downlink(PROTO_T_ENTER_DFU, 7u, magic, PROTO_DFU_MAGIC_LEN,
                       buf, sizeof(buf));
    vk_app_on_usb_bytes(&app, buf, (uint32_t)n);
    VK_CHECK_EQ_INT(vk_stub_reboots, 1u);

    /* WiFi -> 拒绝，不重启 */
    vk_stub_reset();
    vk_app_init(&app);
    n = build_downlink(PROTO_T_ENTER_DFU, 7u, magic, PROTO_DFU_MAGIC_LEN,
                       buf, sizeof(buf));
    vk_app_on_wifi_bytes(&app, buf, (uint32_t)n);
    VK_CHECK_EQ_INT(vk_stub_reboots, 0u);
}

/* --------------------- 语音键 -> 会话启动 --------------------- */
VK_SUITE(app_voice_key)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);
    VK_CHECK_EQ_INT(app.asr.st, VK_ASR_ST_IDLE);

    /* K7 是默认语音键 */
    vk_stub_raw[6] = 1u;
    vk_stub_advance_ms(200u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u);
    (void)vk_app_poll(&app);

    VK_CHECK_EQ_INT(app.asr.st, VK_ASR_ST_CAPTURING);
    VK_CHECK_EQ_INT(vk_stub_last_payload[0], '7');
}

/* --------------------- ASR 文本 -> 0x03 帧 --------------------- */
VK_SUITE(app_asr_text)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);

    vk_stub_frames_sent = 0u;
    vk_app_on_asr_text(&app, "帮我看一下这个函数", 0u);
    VK_CHECK_EQ_INT(vk_stub_last_type, PROTO_T_ASR_TEXT);
    VK_CHECK_EQ_INT(vk_stub_last_payload[0], PROTO_ASR_PARTIAL);
    {
        /* 载荷首字节之后是 UTF-8 文本 */
        const char *txt = (const char *)&vk_stub_last_payload[1];
        VK_CHECK_EQ_STR(txt, "帮我看一下这个函数");
    }

    vk_app_on_asr_text(&app, "帮我看一下这个函数。", 1u);
    VK_CHECK_EQ_INT(vk_stub_last_payload[0], PROTO_ASR_FINAL);
    VK_CHECK_EQ_INT(app.asr.st, VK_ASR_ST_IDLE);
}

/* --------------------- ASR 失败 -> 只警告不降级 --------------------- */
VK_SUITE(app_asr_fail_degrades)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);

    vk_app_on_asr_fail(&app);
    /* 状态必须仍是工作/AP 态，不是 ERROR */
    VK_CHECK(app.state != VK_STATE_ERROR);
    VK_CHECK_EQ_INT(app.status.warn, VK_WARN_ASR_UNAVAILABLE);
}

/* --------------------- 心跳与周期上报 --------------------- */
VK_SUITE(app_heartbeat)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);

    /* 上位机 ping -> 设备回 pong */
    {
        uint8_t buf[16];
        size_t n = build_downlink(PROTO_T_PING, 42u, NULL, 0u, buf, sizeof(buf));
        vk_stub_frames_sent = 0u;
        vk_app_on_usb_bytes(&app, buf, (uint32_t)n);
        VK_CHECK_EQ_INT(vk_stub_frames_sent, 1u);
        VK_CHECK_EQ_INT(vk_stub_last_type, PROTO_T_PONG);
    }

    /* 请求状态 -> 立即上报 */
    {
        uint8_t buf[16];
        size_t n = build_downlink(PROTO_T_STATUS_REQ, 43u, NULL, 0u, buf, sizeof(buf));
        vk_stub_frames_sent = 0u;
        vk_app_on_usb_bytes(&app, buf, (uint32_t)n);
        VK_CHECK_EQ_INT(vk_stub_last_type, PROTO_T_STATUS);
    }

    /* 1 秒后周期性上报 */
    {
        vk_stub_frames_sent = 0u;
        vk_stub_advance_ms(1000u);
        (void)vk_app_poll(&app);
        VK_CHECK(vk_stub_frames_sent >= 1u);
        VK_CHECK_EQ_INT(vk_stub_last_type, PROTO_T_STATUS);
    }
}

/* --------------------- 双通道同一事件同一 SEQ --------------------- */
VK_SUITE(app_seq_shared)
{
    vk_app_t app;
    vk_stub_reset();
    vk_app_init(&app);

    /* 按键事件在 app_send 内部只分配一次 seq，
     * 由 stub 记录最后一帧的 seq，连续两次按键应递增 */
    vk_stub_raw[1] = 1u;
    vk_stub_advance_ms(100u);
    (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u); (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u); (void)vk_app_poll(&app);
    vk_stub_advance_ms(5u); (void)vk_app_poll(&app);
    {
        uint16_t first = vk_stub_last_seq;
        /* 必须先让松开被确认（>=15ms），否则 stable 仍是 1，再按不会产生新边沿 */
        vk_stub_raw[1] = 0u;
        vk_stub_advance_ms(20u); (void)vk_app_poll(&app);
        vk_stub_advance_ms(20u); (void)vk_app_poll(&app);
        vk_stub_raw[1] = 1u;
        vk_stub_advance_ms(50u); (void)vk_app_poll(&app);
        vk_stub_advance_ms(5u);  (void)vk_app_poll(&app);
        vk_stub_advance_ms(5u);  (void)vk_app_poll(&app);
        vk_stub_advance_ms(5u);  (void)vk_app_poll(&app);
        VK_CHECK_EQ_INT(vk_stub_last_type, PROTO_T_KEY_EVENT);
        VK_CHECK(vk_stub_last_seq != first);
    }
}

/* --------------------- 拔路由器 -> 回退 AP --------------------- */
VK_SUITE(app_router_loss)
{
    vk_app_t app;
    int i;
    vk_stub_reset();
    vk_app_init(&app);
    vk_net_on_creds(&app.net, 1u, "MyHome", "password123", 0u);
    strcpy(vk_stub_sta_ip, "192.168.1.23");
    vk_stub_advance_ms(100u);
    (void)vk_app_poll(&app);
    VK_CHECK_EQ_INT(app.state, VK_STATE_WORK_READY);

    /* 拔路由器：IP 消失 */
    vk_stub_sta_ip[0] = '\0';
    for (i = 0; i < 40; i++) {
        vk_stub_advance_ms(1000u);
        (void)vk_app_poll(&app);
    }
    /* 约 30s 后应回退 AP，且不是 ERROR */
    VK_CHECK_EQ_INT(vk_net_state(&app.net), VK_NET_ST_AP);
    VK_CHECK(app.state != VK_STATE_ERROR);
}

void test_app_run(void)
{
    vk_suite_name = "app";
    (void)printf("  [app] 集成：启动配网 / 按键上行 / DFU通道规则 / 语音 / 心跳 / 掉线回退\n");
    suite_app_boot_to_ap();
    suite_app_boot_to_sta();
    suite_app_key_event();
    suite_app_dfu_channel_rule();
    suite_app_voice_key();
    suite_app_asr_text();
    suite_app_asr_fail_degrades();
    suite_app_heartbeat();
    suite_app_seq_shared();
    suite_app_router_loss();
}
