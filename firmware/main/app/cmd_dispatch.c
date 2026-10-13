/**
 * @file cmd_dispatch.c
 * @brief 下行指令分发实现。
 *
 * 烧录态唯一入口的四道校验（设计文档 §7.1）：
 *   1. 帧类型必须是 0x13 且载荷恰好 8 字节
 *   2. 载荷与 magic word 逐字节相等
 *   3. 来源通道必须是 USB（WiFi 来的直接拒绝）
 *   4. 一次性有效：同一 magic 在 window_ms 内重复到达则静默忽略；且设备不得处于 ERROR 态
 */
#include "cmd_dispatch.h"

#include <string.h>
#include <stddef.h>   /* NULL */

#define VK_CMD_DFU_DEFAULT_WINDOW_MS 5000u
#define VK_CMD_SSID_MAX 32u
#define VK_CMD_PASS_MIN 8u
#define VK_CMD_PASS_MAX 64u

void vk_cmd_init(vk_cmd_dispatch_t *d, uint32_t window_ms)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
    d->dfu_window_ms = (window_ms != 0u) ? window_ms : VK_CMD_DFU_DEFAULT_WINDOW_MS;
    d->state = VK_STATE_BOOT;
}

void vk_cmd_set_state(vk_cmd_dispatch_t *d, vk_state_t state)
{
    if (d != NULL) {
        d->state = state;
    }
}

void vk_asr_cfg_default(vk_asr_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->provider, "tencent");
    cfg->sample_rate = 16000u;
    strcpy(cfg->format, "pcm");
    cfg->channels = 1u;
    cfg->voice_key = (char)VK_CHAR_KEY_7;
}

/* ------------------------- ACK/NACK 载荷构造 --------------------------- */
static size_t build_ack(uint8_t *out, size_t cap, uint8_t ok, const char *rsn)
{
    size_t n = 0u;
    if ((out == NULL) || (cap == 0u)) {
        return 0u;
    }
    out[n++] = ok;
    if ((ok == PROTO_ACK_NACK) && (rsn != NULL)) {
        while ((*rsn != '\0') && (n < cap)) {
            out[n++] = (uint8_t)*rsn++;
        }
    }
    return n;
}

/* --------------------------- 载荷解析 ---------------------------------- */

/** 字符是否合法：不能出现 ',' '=' '\0' 之外的空白与控制字符由调用方保证 */
static int has_bad_char(const char *s, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c == (unsigned char)',') || (c == (unsigned char)'=')) {
            return 1;
        }
        if ((c < 0x20u) || (c == 0x7Fu)) {
            return 1;
        }
    }
    return 0;
}

int vk_cmd_parse_wifi(const uint8_t *payload, uint16_t len,
                      char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    size_t ssid_len;
    size_t pass_len;
    size_t i;

    if ((payload == NULL) || (ssid == NULL) || (pass == NULL) || (len == 0u)) {
        return -1;
    }

    /* 结构必须是 SSID '\0' PASS '\0' */
    for (i = 0u; i < (size_t)len; i++) {
        if (payload[i] == 0u) {
            break;
        }
    }
    if (i >= (size_t)len) {
        return -2;                       /* 找不到第一个 NUL */
    }
    ssid_len = i;
    if ((ssid_len == 0u) || (ssid_len > VK_CMD_SSID_MAX) || (ssid_len >= ssid_cap)) {
        return -1;
    }
    if ((size_t)len < ssid_len + 2u) {
        return -2;                       /* 密码段不完整 */
    }
    pass_len = (size_t)len - ssid_len - 2u;
    if ((pass_len < VK_CMD_PASS_MIN) || (pass_len > VK_CMD_PASS_MAX)
        || (pass_len >= pass_cap)) {
        return -1;
    }
    if (payload[(size_t)len - 1u] != 0u) {
        return -2;                       /* 结尾缺少 NUL */
    }

    if (has_bad_char((const char *)payload, ssid_len)
        || has_bad_char((const char *)&payload[ssid_len + 1u], pass_len)) {
        return -3;
    }

    memcpy(ssid, payload, ssid_len);
    ssid[ssid_len] = '\0';
    memcpy(pass, &payload[ssid_len + 1u], pass_len);
    pass[pass_len] = '\0';
    return 0;
}

/** 自己实现，避免 strnlen 在 -std=c99 -Wpedantic 下的可移植性问题 */
static size_t seg_strnlen(const char *s, size_t max)
{
    size_t n = 0u;
    while ((n < max) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

/** 复制一段键值到定长缓冲，超长返回 -1 */
static int copy_val(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) {
        return -1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 0;
}

/** 解析无符号整数，仅接受 ASCII 数字 */
static int parse_uint(const char *s, size_t len, unsigned long *out)
{
    unsigned long v = 0u;
    size_t i;
    if ((len == 0u) || (len > 10u)) {
        return -1;
    }
    for (i = 0u; i < len; i++) {
        if ((s[i] < '0') || (s[i] > '9')) {
            return -1;
        }
        v = v * 10u + (unsigned long)(s[i] - '0');
    }
    *out = v;
    return 0;
}

int vk_cmd_parse_asr(const uint8_t *payload, uint16_t len, vk_asr_cfg_t *cfg)
{
    vk_asr_cfg_t tmp;
    size_t off = 0u;
    int have_provider = 0;
    int have_appid = 0;
    int have_sid = 0;
    int have_skey = 0;

    if ((payload == NULL) || (cfg == NULL) || (len == 0u)) {
        return -1;
    }
    if (payload[(size_t)len - 1u] != 0u) {
        return -2;                       /* 必须以 NUL 结尾 */
    }
    vk_asr_cfg_default(&tmp);

    while (off < (size_t)len) {
        const char *kv = (const char *)&payload[off];
        size_t remain = (size_t)len - off;
        size_t seglen = seg_strnlen(kv, remain);
        const char *eq;
        size_t klen;
        size_t vlen;
        const char *v;

        if (seglen == 0u) {
            off += 1u;                   /* 空段，跳过 */
            continue;
        }
        eq = (const char *)memchr(kv, '=', seglen);
        if ((eq == NULL) || (eq == kv)) {
            return -2;
        }
        klen = (size_t)(eq - kv);
        vlen = seglen - klen - 1u;
        if (vlen == 0u) {
            return -2;
        }
        v = eq + 1;

        if ((klen == 8u) && (memcmp(kv, "provider", 8u) == 0)) {
            if ((copy_val(tmp.provider, sizeof(tmp.provider), v, vlen) != 0)
                || (vlen == 0u)) { return -3; }
            have_provider = 1;
        } else if ((klen == 5u) && (memcmp(kv, "appid", 5u) == 0)) {
            if (copy_val(tmp.appid, sizeof(tmp.appid), v, vlen) != 0) { return -3; }
            have_appid = 1;
        } else if ((klen == 9u) && (memcmp(kv, "secret_id", 9u) == 0)) {
            if (copy_val(tmp.secret_id, sizeof(tmp.secret_id), v, vlen) != 0) { return -3; }
            have_sid = 1;
        } else if ((klen == 10u) && (memcmp(kv, "secret_key", 10u) == 0)) {
            if (copy_val(tmp.secret_key, sizeof(tmp.secret_key), v, vlen) != 0) { return -3; }
            have_skey = 1;
        } else if ((klen == 11u) && (memcmp(kv, "sample_rate", 11u) == 0)) {
            unsigned long r;
            if ((parse_uint(v, vlen, &r) != 0)
                || ((r != 8000uL) && (r != 16000uL) && (r != 44100uL)
                    && (r != 48000uL))) {
                return -2;
            }
            tmp.sample_rate = (uint32_t)r;
        } else if ((klen == 6u) && (memcmp(kv, "format", 6u) == 0)) {
            if (copy_val(tmp.format, sizeof(tmp.format), v, vlen) != 0) { return -3; }
        } else if ((klen == 8u) && (memcmp(kv, "channels", 8u) == 0)) {
            unsigned long ch;
            if ((parse_uint(v, vlen, &ch) != 0) || ((ch != 1uL) && (ch != 2uL))) {
                return -2;
            }
            tmp.channels = (uint8_t)ch;
        } else if ((klen == 9u) && (memcmp(kv, "voice_key", 9u) == 0)) {
            if (vlen != 1u) { return -2; }
            tmp.voice_key = (char)v[0];
        }
        /* 未识别的键忽略，保证前向兼容 */

        off += seglen + 1u;
    }

    if ((have_provider == 0) || (have_appid == 0) || (have_sid == 0) || (have_skey == 0)) {
        return -1;
    }
    *cfg = tmp;
    return 0;
}

/**
 * 从 0x12 帧载荷里扫出 cloud_url / cloud_token（云连接配置）。
 * 这两个键不属于 ASR 配置（不在 vk_asr_cfg_t 里），
 * 所以单独扫描并存进 cmd_dispatch_t 的云字段。
 * 复用 0x12 帧的扩展键，不改协议、不加新帧类型。
 */
static void scan_cloud_keys(const uint8_t *payload, uint16_t len, vk_cmd_dispatch_t *d)
{
    size_t off = 0u;
    if ((payload == NULL) || (d == NULL) || (len == 0u)) {
        return;
    }

    while (off < (size_t)len) {
        const char *kv = (const char *)&payload[off];
        size_t remain = (size_t)len - off;
        size_t seglen = seg_strnlen(kv, remain);
        const char *eq;
        size_t klen;
        size_t vlen;
        const char *v;

        if (seglen == 0u) {
            off += 1u;
            continue;
        }
        eq = (const char *)memchr(kv, '=', seglen);
        if ((eq == NULL) || (eq == kv)) {
            off += seglen + 1u;
            continue;
        }
        klen = (size_t)(eq - kv);
        vlen = seglen - klen - 1u;
        v = eq + 1;

        if ((klen == 9u) && (memcmp(kv, "cloud_url", 9u) == 0)) {
            if (vlen < sizeof(d->cloud_url)) {
                memcpy(d->cloud_url, v, vlen);
                d->cloud_url[vlen] = '\0';
            }
        } else if ((klen == 11u) && (memcmp(kv, "cloud_token", 11u) == 0)) {
            if (vlen < sizeof(d->cloud_token)) {
                memcpy(d->cloud_token, v, vlen);
                d->cloud_token[vlen] = '\0';
            }
        }

        off += seglen + 1u;
    }
}

/* ----------------------------- 分发主逻辑 ------------------------------ */

vk_cmd_action_t vk_cmd_handle(vk_cmd_dispatch_t *d, const proto_frame_t *f, uint8_t channel,
                              uint32_t now_ms,
                              uint8_t *ack_out, size_t ack_cap, size_t *ack_len)
{
    vk_cmd_action_t act = VK_CMD_ACT_NONE;
    size_t alen = 0u;
    int ok = 1;   /* 1 = ACK，0 = NACK */
    const char *rsn = NULL;
    int silent = 0;   /* 1 = 不回 ACK/NACK（已被更具体的应答取代） */

    if ((d == NULL) || (f == NULL)) {
        return VK_CMD_ACT_NONE;
    }
    if (ack_len != NULL) {
        *ack_len = 0u;
    }

    switch (f->type) {

    case PROTO_T_PING:
        /* 回 pong 而不是 ACK——pong 本身就是心跳应答 */
        act = VK_CMD_ACT_PONG;
        silent = 1;
        break;

    case PROTO_T_WIFI_CFG:
        if (vk_cmd_parse_wifi(f->payload, f->len, d->wifi_ssid,
                              sizeof(d->wifi_ssid), d->wifi_pass,
                              sizeof(d->wifi_pass)) != 0) {
            ok = 0;
            rsn = VK_CMD_RSN_FMT;
        } else {
            act = VK_CMD_ACT_WIFI_SAVE;
        }
        break;

    case PROTO_T_ASR_CFG:
        if (vk_cmd_parse_asr(f->payload, f->len, &d->asr_cfg) != 0) {
            ok = 0;
            rsn = VK_CMD_RSN_FMT;
        } else {
            scan_cloud_keys(f->payload, f->len, d);   /* 顺带扫云配置（cloud_url/cloud_token） */
            act = VK_CMD_ACT_ASR_SAVE;
        }
        break;

    case PROTO_T_VOICE_CTL:
        if (f->len != 1u) {
            ok = 0;
            rsn = VK_CMD_RSN_LEN;
        } else if (f->payload[0] == (uint8_t)PROTO_VOICE_START) {
            act = VK_CMD_ACT_VOICE_START;
        } else if (f->payload[0] == (uint8_t)PROTO_VOICE_STOP) {
            act = VK_CMD_ACT_VOICE_STOP;
        } else {
            ok = 0;
            rsn = VK_CMD_RSN_FMT;
        }
        break;

    case PROTO_T_STATUS_REQ:
        /* 状态帧本身就是应答，不再额外回 ACK */
        act = VK_CMD_ACT_STATUS_REQ;
        silent = 1;
        break;

    case PROTO_T_ENTER_DFU:
        /* ---- 烧录态唯一入口：四道校验，全部通过才放行 ---- */
        if (channel != (uint8_t)VK_CH_USB) {
            /* PRD：WiFi 报文不能触发烧录态 */
            ok = 0;
            rsn = VK_CMD_RSN_CH;
        } else if (f->len != (uint16_t)PROTO_DFU_MAGIC_LEN) {
            ok = 0;
            rsn = VK_CMD_RSN_LEN;
        } else if (memcmp(f->payload, proto_dfu_magic, PROTO_DFU_MAGIC_LEN) != 0) {
            ok = 0;
            rsn = VK_CMD_RSN_MAGIC;
        } else if (d->state == VK_STATE_ERROR) {
            ok = 0;
            rsn = VK_CMD_RSN_STATE;
        } else if ((d->dfu_consumed != 0u)
                   && ((now_ms - d->dfu_consumed_at_ms) < d->dfu_window_ms)) {
            /* 重放：静默忽略，不回 ACK，避免给攻击者反馈 */
            silent = 1;
            rsn = VK_CMD_RSN_REPLAY;
        } else {
            d->dfu_consumed = 1u;
            d->dfu_consumed_at_ms = now_ms;
            act = VK_CMD_ACT_ENTER_DFU;
        }
        break;

    case PROTO_T_VER:
        /* 版本协商：本期只有 V1，直接 ACK */
        break;

    default:
        ok = 0;
        rsn = VK_CMD_RSN_TYPE;
        break;
    }

    if (silent == 0) {
        alen = build_ack(ack_out, ack_cap,
                         (ok != 0) ? (uint8_t)PROTO_ACK_OK : (uint8_t)PROTO_ACK_NACK,
                         rsn);
    }
    if (ack_len != NULL) {
        *ack_len = alen;
    }
    return act;
}
