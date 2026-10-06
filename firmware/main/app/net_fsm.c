/**
 * @file net_fsm.c
 * @brief 配网状态机实现。
 *
 * 关键行为（PRD 5.2 验收口径）：
 *   - 空 NVS 上电 -> 直接 AP 模式
 *   - 凭据错误   -> 重试达上限后回退 AP（约 40s 内，不是故障，不红灯常亮）
 *   - 已连接时拔路由器 -> 重试窗口结束后回退 AP，不死机
 *   - 配网时半途断开 -> 不写 NVS，不重启（由 app 保证：只有收到完整合法帧才调 new_config）
 */
#include "net_fsm.h"

#include <string.h>
#include <stddef.h>   /* NULL */

#define VK_NET_AP_PREFIX "VibeKey-"

vk_net_cfg_t vk_net_default_cfg(void)
{
    vk_net_cfg_t c;
    c.sta_timeout_ms = VK_NET_STA_TIMEOUT_MS;
    c.sta_retry_max  = VK_NET_STA_RETRY_MAX;
    /* 占位值，量产前必须替换为开发者生成的随机串（设计文档 §7.2） */
    strcpy(c.ap_pass, "Vb7Kq2mXv9");
    return c;
}

void vk_net_init(vk_net_fsm_t *f, const vk_net_cfg_t *cfg, const char *mac4)
{
    if (f == NULL) {
        return;
    }
    f->cfg = (cfg != NULL) ? *cfg : vk_net_default_cfg();
    if (f->cfg.sta_timeout_ms == 0u) { f->cfg.sta_timeout_ms = VK_NET_STA_TIMEOUT_MS; }
    if (f->cfg.sta_retry_max == 0u)  { f->cfg.sta_retry_max = VK_NET_STA_RETRY_MAX; }

    f->st = VK_NET_ST_INIT;
    f->retry_count = 0u;
    f->state_since_ms = 0u;
    f->last_attempt_ms = 0u;
    f->rssi = 0;
    f->ip[0] = '\0';
    f->ssid[0] = '\0';
    f->pass[0] = '\0';

    /* AP SSID = "VibeKey-" + MAC 后 4 位 */
    f->ap_ssid[0] = '\0';
    strncat(f->ap_ssid, VK_NET_AP_PREFIX, sizeof(f->ap_ssid) - 1u);
    if (mac4 != NULL) {
        strncat(f->ap_ssid, mac4,
                sizeof(f->ap_ssid) - strlen(f->ap_ssid) - 1u);
    }
}

vk_net_state_t vk_net_state(const vk_net_fsm_t *f)
{
    return (f == NULL) ? VK_NET_ST_INIT : f->st;
}

int vk_net_is_up(const vk_net_fsm_t *f)
{
    if (f == NULL) {
        return 0;
    }
    return ((f->st == VK_NET_ST_STA_CONNECTING)
            || (f->st == VK_NET_ST_CONNECTED)) ? 1 : 0;
}

const char *vk_net_state_str(vk_net_state_t s)
{
    switch (s) {
    case VK_NET_ST_INIT:            return "INIT";
    case VK_NET_ST_STA_CONNECTING:  return "STA_CONNECTING";
    case VK_NET_ST_CONNECTED:       return "CONNECTED";
    case VK_NET_ST_AP:              return "AP";
    default:                        return "?";
    }
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if ((dst == NULL) || (cap == 0u)) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

vk_net_action_t vk_net_on_creds(vk_net_fsm_t *f, uint8_t have_creds,
                                const char *ssid, const char *pass,
                                uint32_t now_ms)
{
    if (f == NULL) {
        return VK_NET_ACT_NONE;
    }
    if (have_creds != 0u) {
        copy_str(f->ssid, sizeof(f->ssid), ssid);
        copy_str(f->pass, sizeof(f->pass), pass);
        f->st = VK_NET_ST_STA_CONNECTING;
        f->retry_count = 0u;
        f->state_since_ms = now_ms;
        f->last_attempt_ms = now_ms;
        return VK_NET_ACT_START_STA;
    }
    f->st = VK_NET_ST_AP;
    f->retry_count = 0u;
    f->state_since_ms = now_ms;
    return VK_NET_ACT_START_AP;
}

vk_net_action_t vk_net_tick(vk_net_fsm_t *f, uint32_t now_ms, uint8_t sta_up,
                            const char *ip, int rssi)
{
    if (f == NULL) {
        return VK_NET_ACT_NONE;
    }
    f->rssi = rssi;
    if (ip != NULL) {
        copy_str(f->ip, sizeof(f->ip), ip);
    } else {
        f->ip[0] = '\0';
    }

    switch (f->st) {

    case VK_NET_ST_STA_CONNECTING:
        if (sta_up != 0u) {
            f->st = VK_NET_ST_CONNECTED;
            f->retry_count = 0u;
            f->state_since_ms = now_ms;
            return VK_NET_ACT_NONE;
        }
        if ((now_ms - f->state_since_ms) >= f->cfg.sta_timeout_ms) {
            f->retry_count++;
            if (f->retry_count >= f->cfg.sta_retry_max) {
                /* 重试耗尽 -> 回退 AP。PRD 明确：这不算故障，不红灯常亮 */
                f->st = VK_NET_ST_AP;
                f->retry_count = 0u;
                f->state_since_ms = now_ms;
                return VK_NET_ACT_START_AP;
            }
            /* 关键：重置窗口起点，否则 3 次重试会在同一毫秒内全部耗尽 */
            f->state_since_ms = now_ms;
            return VK_NET_ACT_START_STA;   /* 再试一次 */
        }
        return VK_NET_ACT_NONE;

    case VK_NET_ST_CONNECTED:
        if (sta_up == 0u) {
            /* 掉线（如拔路由器）：重新进入尝试，归零重试计数 */
            f->st = VK_NET_ST_STA_CONNECTING;
            f->retry_count = 0u;
            f->state_since_ms = now_ms;
            return VK_NET_ACT_START_STA;
        }
        return VK_NET_ACT_NONE;

    case VK_NET_ST_AP:
    case VK_NET_ST_INIT:
    default:
        return VK_NET_ACT_NONE;
    }
}

vk_net_action_t vk_net_new_config(vk_net_fsm_t *f, const char *ssid,
                                  const char *pass, uint32_t now_ms)
{
    if (f == NULL) {
        return VK_NET_ACT_NONE;
    }
    copy_str(f->ssid, sizeof(f->ssid), ssid);
    copy_str(f->pass, sizeof(f->pass), pass);
    f->state_since_ms = now_ms;
    return VK_NET_ACT_SAVE_CREDS;
}

vk_net_action_t vk_net_on_saved(vk_net_fsm_t *f, uint32_t now_ms)
{
    if (f == NULL) {
        return VK_NET_ACT_NONE;
    }
    f->state_since_ms = now_ms;
    return VK_NET_ACT_REBOOT;
}
