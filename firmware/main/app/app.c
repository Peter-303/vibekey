/**
 * @file app.c
 * @brief 应用层主调度器实现。
 *
 * 这是 app/ 下唯一"有副作用编排"的模块：它决定何时调用 vk_port 的哪个函数。
 * 所有判断逻辑都在各状态机里，这里只做粘合，因此可用 stub_port 完整测试。
 */
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "led_fsm.h"
#include "port.h"
#include <stddef.h>   /* NULL */

/* ---------------------------- 内部辅助 --------------------------------- */

static void app_do_asr_action(vk_app_t *app, vk_asr_action_t act);
static void app_do_net_action(vk_app_t *app, vk_net_action_t nact);

/** 喂事件给语音状态机并执行其要求的动作。 */
static void app_asr_event(vk_app_t *app, vk_asr_event_t ev)
{
    app_do_asr_action(app, vk_asr_event(&app->asr, ev));
}

/** 发送一个上行帧到双通道（USB + WiFi），使用给定 seq。 */
static void app_send(vk_app_t *app, uint8_t type, const uint8_t *payload, uint16_t len,
                     uint16_t seq)
{
    proto_frame_t f;
    (void)app;   /* 保留参数以保持接口稳定；当前无需访问 app 上下文 */
    uint8_t buf[PROTO_HEADER_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN];
    size_t out_len = 0u;

    if (vk_port == NULL) {
        return;
    }
    f.type = type;
    f.seq = seq;
    f.len = len;
    if ((len > 0u) && (payload != NULL)) {
        if (len > PROTO_MAX_PAYLOAD) {
            len = PROTO_MAX_PAYLOAD;
            f.len = len;
        }
        memcpy(f.payload, payload, len);
    }
    if (proto_encode(&f, buf, sizeof(buf), &out_len) != 0) {
        return;
    }
    /* 同一 seq 同时走两条通道（设计文档 §3.4 规则 2） */
    vk_port->send_frame(buf, out_len);
}

/** 发送 ACK/NACK（上行 0x06），回带下行帧的 seq。 */
static void app_send_ack(vk_app_t *app, uint16_t seq, uint8_t ok, const char *rsn)
{
    uint8_t pl[24];
    size_t n = 0u;
    pl[n++] = ok;
    if ((ok != PROTO_ACK_OK) && (rsn != NULL)) {
        while ((*rsn != '\0') && (n < sizeof(pl))) {
            pl[n++] = (uint8_t)*rsn++;
        }
    }
    app_send(app, PROTO_T_ACK, pl, (uint16_t)n, seq);
}

/** 上报当前状态（USB/WiFi 的 0x02 与 UART1 用同一个格式化函数）。 */
static void app_report_status(vk_app_t *app)
{
    char line[160];
    int n;

    app->status.state = app->state;
    n = vk_status_format(&app->status, line, sizeof(line));
    if (n > 0) {
        app_send(app, PROTO_T_STATUS, (const uint8_t *)line, (uint16_t)n,
                 vk_seq_next(&app->seq));
    }
    if ((vk_port != NULL) && (vk_port->uart1_print != NULL)) {
        if (vk_status_format_uart1(&app->status, line, sizeof(line)) > 0) {
            vk_port->uart1_print(line);
        }
    }
    app->status_dirty = 0u;
}

/** 安全拷贝字符串（net_fsm.c 里的同名函数是 static，此处需自带）。 */
static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if ((dst == NULL) || (cap == 0u)) {
        return;
    }
    if (src == NULL) {
        dst[0] = 0;
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

/** 把 net_fsm 的状态映射成 vk_state_t。 */
static vk_state_t app_state_from_net(const vk_net_fsm_t *net)
{
    switch (vk_net_state(net)) {
    case VK_NET_ST_AP:             return VK_STATE_AP_START;
    case VK_NET_ST_CONNECTED:      return VK_STATE_WORK_READY;
    case VK_NET_ST_STA_CONNECTING: return VK_STATE_STA_JOIN_OK;
    case VK_NET_ST_INIT:
    default:                       return VK_STATE_BOOT;
    }
}

/** 执行 net_fsm 返回的动作（boot 与 poll 共用）。 */
static void app_do_net_action(vk_app_t *app, vk_net_action_t nact)
{
    switch (nact) {
    case VK_NET_ACT_START_STA:
        if ((vk_port != NULL) && (vk_port->wifi_stop != NULL)) { vk_port->wifi_stop(); }
        if ((vk_port != NULL) && (vk_port->wifi_start_sta != NULL)) {
            (void)vk_port->wifi_start_sta(app->net.ssid, app->net.pass);
        }
        vk_app_set_state(app, VK_STATE_STA_JOIN_OK);
        break;
    case VK_NET_ACT_START_AP:
        if ((vk_port != NULL) && (vk_port->wifi_stop != NULL)) { vk_port->wifi_stop(); }
        if ((vk_port != NULL) && (vk_port->wifi_start_ap != NULL)) {
            (void)vk_port->wifi_start_ap(app->net.ap_ssid, app->net.cfg.ap_pass);
        }
        vk_app_set_state(app, VK_STATE_AP_START);
        break;
    case VK_NET_ACT_SAVE_CREDS:
    case VK_NET_ACT_REBOOT:
    case VK_NET_ACT_NONE:
    default:
        /* SAVE_CREDS / REBOOT 由 cmd 路径处理 */
        break;
    }
}

/** 应用 LED（纯函数，500ms 内生效的要求天然满足）。 */
static void app_apply_led(vk_app_t *app)
{
    vk_led_output_t o;
    if ((vk_port == NULL) || (vk_port->led_set == NULL)) {
        return;
    }
    o = vk_led_output(app->state, vk_port->now_ms());
    vk_port->led_set(o.blue_on, o.red_on);
}

/** 执行 cmd_dispatch 返回的动作。 */
static void app_do_cmd_action(vk_app_t *app, vk_cmd_action_t act)
{
    switch (act) {

    case VK_CMD_ACT_WIFI_SAVE:
        /* 先写 NVS 并 commit，成功才重启（PRD 8 章：半途断开不写半成品） */
        if ((vk_port != NULL) && (vk_port->nvs_set_str != NULL)
            && (vk_port->nvs_commit != NULL)) {
            if ((vk_port->nvs_set_str("wifi_ssid", app->cmd.wifi_ssid) == VK_PORT_OK)
                && (vk_port->nvs_set_str("wifi_pass", app->cmd.wifi_pass) == VK_PORT_OK)
                && (vk_port->nvs_commit() == VK_PORT_OK)) {
                if (vk_net_on_saved(&app->net, vk_port->now_ms())
                        == VK_NET_ACT_REBOOT) {
                    if (vk_port->reboot != NULL) {
                        vk_port->reboot();
                    }
                }
            } else {
                app->status.err = VK_ERR_NVS_FAIL;
                app->status_dirty = 1u;
            }
        }
        break;

    case VK_CMD_ACT_ASR_SAVE:
        /* ASR 凭据一并进 NVS，与 WiFi 信息统一管理（PRD 5.6） */
        if ((vk_port != NULL) && (vk_port->nvs_set_str != NULL)
            && (vk_port->nvs_commit != NULL)) {
            char tmp[16];
            (void)vk_port->nvs_set_str("asr_provider", app->cmd.asr_cfg.provider);
            (void)vk_port->nvs_set_str("asr_appid", app->cmd.asr_cfg.appid);
            (void)vk_port->nvs_set_str("asr_sid", app->cmd.asr_cfg.secret_id);
            (void)vk_port->nvs_set_str("asr_skey", app->cmd.asr_cfg.secret_key);
            (void)snprintf(tmp, sizeof(tmp), "%lu",
                           (unsigned long)app->cmd.asr_cfg.sample_rate);
            (void)vk_port->nvs_set_str("asr_rate", tmp);
            (void)vk_port->nvs_set_str("asr_format", app->cmd.asr_cfg.format);
            (void)snprintf(tmp, sizeof(tmp), "%u", (unsigned)app->cmd.asr_cfg.channels);
            (void)vk_port->nvs_set_str("asr_ch", tmp);
            {
                char vk[2];
                vk[0] = app->cmd.asr_cfg.voice_key;
                vk[1] = '\0';
                (void)vk_port->nvs_set_str("asr_vkey", vk);
            }
            if (vk_port->nvs_commit() != VK_PORT_OK) {
                app->status.err = VK_ERR_NVS_FAIL;
                app->status_dirty = 1u;
            }
            /* 语音键可能被重新指定，同步给按键状态机 */
            app->keys.cfg.voice_key_char = app->cmd.asr_cfg.voice_key;
        }
        break;

    case VK_CMD_ACT_ENTER_DFU:
        /* 先让用户看到 DOWNLOAD 状态，再重启进下载模式 */
        app_send(app, PROTO_T_STATUS,
                 (const uint8_t *)"DOWNLOAD", 8u, vk_seq_next(&app->seq));
        if (vk_port != NULL) {
            if (vk_port->nvs_commit != NULL) {
                (void)vk_port->nvs_commit();
            }
            if (vk_port->reboot != NULL) {
                vk_port->reboot();
            }
        }
        break;

    case VK_CMD_ACT_VOICE_START:
        app_asr_event(app, VK_ASR_EV_VOICE_DOWN);
        break;

    case VK_CMD_ACT_VOICE_STOP:
        app_asr_event(app, VK_ASR_EV_VOICE_UP);
        break;

    case VK_CMD_ACT_STATUS_REQ:
        /* 上位机明确请求时应立即上报，不能等下一次 poll */
        app->status_dirty = 1u;
        app_report_status(app);
        break;

    case VK_CMD_ACT_PONG:
        app_send(app, PROTO_T_PONG, NULL, 0u, vk_seq_next(&app->seq));
        break;

    case VK_CMD_ACT_NONE:
    default:
        break;
    }
}

/** 执行 asr_fsm 返回的动作。 */
static void app_do_asr_action(vk_app_t *app, vk_asr_action_t act)
{
    switch (act) {

    case VK_ASR_ACT_MIC_START:
        if ((vk_port != NULL) && (vk_port->asr_start != NULL)) {
            if (vk_port->asr_start((uint8_t)app->asr_mode) != VK_PORT_OK) {
                (void)vk_asr_event(&app->asr, VK_ASR_EV_MIC_ERROR);
            }
        }
        break;

    case VK_ASR_ACT_MIC_STOP:
        if ((vk_port != NULL) && (vk_port->asr_stop != NULL)) {
            vk_port->asr_stop();
        }
        break;

    case VK_ASR_ACT_WARN_UNAVAIL:
        /* 关键：不进 ERROR 态，LED 不变，只上报告警 */
        app->status.warn = VK_WARN_ASR_UNAVAILABLE;
        app->status.asr = VK_ASR_DOWN;
        app->status_dirty = 1u;
        break;

    case VK_ASR_ACT_ERROR_MIC:
        /* 麦克风是硬件故障，进 ERROR 态红灯常亮 */
        app->status.err = VK_ERR_MIC_FAIL;
        app->status.asr = VK_ASR_DOWN;
        vk_app_set_state(app, VK_STATE_ERROR);
        break;

    case VK_ASR_ACT_NONE:
    default:
        break;
    }
}

/* ------------------------------ 公开接口 -------------------------------- */

void vk_app_init(vk_app_t *app)
{
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof(*app));

    app->release_enabled = 0u;
    app->asr_mode = VK_ASR_MODE_STREAM;
    app->mac4[0] = '\0';

    vk_net_init(&app->net, NULL, app->mac4);
    vk_asr_init(&app->asr, app->asr_mode);
    vk_cmd_init(&app->cmd, 0u);

    /* 注意：必须在 vk_cmd_init 之后调用——vk_cmd_init 会 memset 整个结构体，
     * 在此之前设置 voice_key 会被清掉（这个顺序 bug 曾导致语音键完全不触发）。 */
    vk_asr_cfg_default(&app->cmd.asr_cfg);

    {
        vk_key_fsm_cfg_t kc = vk_key_fsm_default_cfg();
        kc.release_enabled = app->release_enabled;
        kc.voice_key_char = app->cmd.asr_cfg.voice_key;
        vk_key_fsm_init(&app->keys, &kc);
    }
    vk_seq_init(&app->seq);
    proto_stream_init(&app->usb_stream);
    proto_stream_init(&app->wifi_stream);
    vk_status_init(&app->status);

    app->state = VK_STATE_BOOT;
    app->prev_state = VK_STATE_BOOT;
    app->status_dirty = 1u;
    app->boot_ms = (vk_port != NULL) ? vk_port->now_ms() : 0u;
    app->last_hb_ms = app->boot_ms;
    app->last_tick_ms = app->boot_ms;
    app->inited = 1u;

    /* 立即上报一次 BOOT，让串口助手第一眼就看到 */
    app_report_status(app);

    /* ---- 启动配网：读 NVS 凭据，有则连 STA，无则进 AP ---- */
    if ((vk_port != NULL) && (vk_port->nvs_get_str != NULL)) {
        char ssid[VK_NET_SSID_MAX + 1u];
        char pass[VK_NET_PASS_MAX + 1u];
        int r1 = vk_port->nvs_get_str("wifi_ssid", ssid, sizeof(ssid));
        int r2 = vk_port->nvs_get_str("wifi_pass", pass, sizeof(pass));
        if ((r1 == VK_PORT_OK) && (r2 == VK_PORT_OK) && (ssid[0] != '\0')) {
            app_do_net_action(app, vk_net_on_creds(&app->net, 1u, ssid, pass,
                                                   vk_port->now_ms()));
        } else {
            app_do_net_action(app, vk_net_on_creds(&app->net, 0u, NULL, NULL,
                                                   vk_port->now_ms()));
        }
    } else {
        app_do_net_action(app, vk_net_on_creds(&app->net, 0u, NULL, NULL, 0u));
    }
    app->status_dirty = 1u;

    /* 上电后立刻让 LED 反映当前状态，不等第一次 poll */
    app_apply_led(app);
}

vk_state_t vk_app_state(const vk_app_t *app)
{
    return (app == NULL) ? VK_STATE_BOOT : app->state;
}

void vk_app_set_state(vk_app_t *app, vk_state_t st)
{
    if ((app == NULL) || (app->state == st)) {
        return;
    }
    app->prev_state = app->state;
    app->state = st;
    app->status.state = st;
    app->status_dirty = 1u;
    vk_cmd_set_state(&app->cmd, st);
}

void vk_app_on_frame(vk_app_t *app, const proto_frame_t *f, uint8_t channel)
{
    uint8_t ack[32];
    size_t ack_len = 0u;
    vk_cmd_action_t act;

    if ((app == NULL) || (f == NULL)) {
        return;
    }
    if (proto_type_is_downlink(f->type) == 0) {
        return;   /* 上行业务帧不该由这里处理 */
    }
    act = vk_cmd_handle(&app->cmd, f, channel,
                        (vk_port != NULL) ? vk_port->now_ms() : 0u,
                        ack, sizeof(ack), &ack_len);
    app_do_cmd_action(app, act);
    if (ack_len > 0u) {
        app_send_ack(app, f->seq, ack[0], (ack[0] == PROTO_ACK_NACK)
                     ? (const char *)&ack[1] : NULL);
    }
}

void vk_app_on_usb_bytes(vk_app_t *app, const uint8_t *data, uint32_t len)
{
    proto_frame_t frames[4];
    size_t cap = 4u;
    proto_dec_result_t r;
    size_t i;

    if ((app == NULL) || (data == NULL) || (len == 0u)) {
        return;
    }
    r = proto_stream_feed(&app->usb_stream, data, (size_t)len, frames, &cap);
    (void)r;
    for (i = 0u; i < cap; i++) {
        vk_app_on_frame(app, &frames[i], (uint8_t)VK_CH_USB);
    }
}

void vk_app_on_wifi_bytes(vk_app_t *app, const uint8_t *data, uint32_t len)
{
    proto_frame_t frames[4];
    size_t cap = 4u;
    proto_dec_result_t r;
    size_t i;

    if ((app == NULL) || (data == NULL) || (len == 0u)) {
        return;
    }
    r = proto_stream_feed(&app->wifi_stream, data, (size_t)len, frames, &cap);
    (void)r;
    for (i = 0u; i < cap; i++) {
        vk_app_on_frame(app, &frames[i], (uint8_t)VK_CH_WIFI);
    }
}

void vk_app_on_asr_text(vk_app_t *app, const char *utf8_text, uint8_t is_final)
{
    size_t n;
    uint8_t pl[PROTO_MAX_PAYLOAD];

    if ((app == NULL) || (utf8_text == NULL)) {
        return;
    }
    n = strlen(utf8_text);
    if (n > (PROTO_MAX_PAYLOAD - 1u)) {
        n = PROTO_MAX_PAYLOAD - 1u;   /* 超长按上限截断，避免分片复杂度 */
    }
    pl[0] = (is_final != 0u) ? (uint8_t)PROTO_ASR_FINAL : (uint8_t)PROTO_ASR_PARTIAL;
    memcpy(&pl[1], utf8_text, n);

    if (is_final != 0u) {
        app_asr_event(app, VK_ASR_EV_ASR_DONE);
        app->status.warn = VK_WARN_NONE;
        app->status.asr = VK_ASR_OK;
        app->status_dirty = 1u;
    }
    app_send(app, PROTO_T_ASR_TEXT, pl, (uint16_t)(n + 1u), vk_seq_next(&app->seq));
}

void vk_app_on_asr_fail(vk_app_t *app)
{
    if (app == NULL) {
        return;
    }
    app_asr_event(app, VK_ASR_EV_ASR_FAIL);
}

void vk_app_on_mic_error(vk_app_t *app)
{
    if (app == NULL) {
        return;
    }
    app_asr_event(app, VK_ASR_EV_MIC_ERROR);
}

int vk_app_poll(vk_app_t *app)
{
    uint32_t now;
    uint8_t raw[VK_RAW_INPUT_COUNT];
    vk_key_event_t ev[8];
    int nev;
    int i;
    vk_net_action_t nact;

    if ((app == NULL) || (app->inited == 0u) || (vk_port == NULL)) {
        return -1;
    }
    now = vk_port->now_ms();

    /* 看门狗 */
    if (vk_port->wdt_feed != NULL) {
        vk_port->wdt_feed();
    }

    /* ---------- 1. 按键扫描 ---------- */
    if (vk_port->keys_sample != NULL) {
        vk_port->keys_sample(raw);
    } else {
        memset(raw, 0, sizeof(raw));
    }
    nev = vk_key_fsm_tick(&app->keys, raw, now, ev, 8u);
    for (i = 0; i < nev; i++) {
        char ch = ev[i].ch;
        if (ch == '\0') {
            continue;
        }
        if (ev[i].kind == VK_KEY_EV_PRESS) {
            /* 同一事件只分配一个 seq，双通道共用（设计文档 §3.4） */
            uint16_t seq = vk_seq_next(&app->seq);
            app_send(app, PROTO_T_KEY_EVENT, (const uint8_t *)&ch, 1u, seq);

            /* 语音键按下 -> 触发语音会话 */
            if (ch == app->cmd.asr_cfg.voice_key) {
                app_asr_event(app, VK_ASR_EV_VOICE_DOWN);
            }
        } else if (ev[i].kind == VK_KEY_EV_RELEASE) {
            uint16_t seq = vk_seq_next(&app->seq);
            app_send(app, PROTO_T_KEY_RELEASE, (const uint8_t *)&ch, 1u, seq);
            if (ch == app->cmd.asr_cfg.voice_key) {
                app_asr_event(app, VK_ASR_EV_VOICE_UP);
            }
        }
    }

    /* ---------- 2. 配网状态机 ---------- */
    {
        uint8_t sta_up = 0u;
        const char *ip = NULL;
        int rssi = 0;
        if (vk_port->wifi_ip != NULL) {
            static char ipbuf[16];
            if (vk_port->wifi_ip(ipbuf, sizeof(ipbuf)) == VK_PORT_OK) {
                if (ipbuf[0] != '\0') {
                    sta_up = 1u;
                    ip = ipbuf;
                }
            }
        }
        if ((sta_up != 0u) && (vk_port->wifi_rssi != NULL)) {
            rssi = vk_port->wifi_rssi();
        }

        nact = vk_net_tick(&app->net, now, sta_up, ip, rssi);
        app_do_net_action(app, nact);

        /* 网络信息同步进状态快照，让上位机状态卡能显示 IP / 信号强度 */
        copy_str(app->status.ip, sizeof(app->status.ip), app->net.ip);
        copy_str(app->status.ssid, sizeof(app->status.ssid), app->net.ssid);
        app->status.rssi = app->net.rssi;

        /* 网络状态 -> 设备状态 */
        if (app->state != VK_STATE_ERROR) {
            vk_app_set_state(app, app_state_from_net(&app->net));
        }
    }

    /* ---------- 3. 语音会话（无待处理事件时维持现状） ---------- */

    /* ---------- 4. 周期上报 + LED ---------- */
    if ((now - app->last_hb_ms) >= VK_APP_HEARTBEAT_MS) {
        app->last_hb_ms = now;
        app->status_dirty = 1u;
    }
    if (app->status_dirty != 0u) {
        app_report_status(app);
    }
    app_apply_led(app);

    /* ---------- 5. 让出 CPU ---------- */
    if (vk_port->sleep_ms != NULL) {
        vk_port->sleep_ms(VK_APP_TICK_MS);
    }
    return 0;
}
