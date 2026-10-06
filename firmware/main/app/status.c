/**
 * @file status.c
 * @brief 状态码表与上报字符串格式化。
 */
#include "status.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>   /* NULL */

const char *vk_state_str(vk_state_t s)
{
    switch (s) {
    case VK_STATE_BOOT:          return "BOOT";
    case VK_STATE_AP_START:      return "AP_START";
    case VK_STATE_STA_JOIN_OK:   return "STA_JOIN_OK";
    case VK_STATE_STA_JOIN_FAIL: return "STA_JOIN_FAIL";
    case VK_STATE_WORK_READY:    return "WORK_READY";
    case VK_STATE_ERROR:         return "ERROR";
    case VK_STATE_DOWNLOAD:      return "DOWNLOAD";
    default:                     return "UNKNOWN";
    }
}

const char *vk_err_str(vk_err_t e)
{
    switch (e) {
    case VK_ERR_WIFI_FAIL:      return "WIFI_FAIL";
    case VK_ERR_MIC_FAIL:       return "MIC_FAIL";
    case VK_ERR_ASR_INIT_FAIL:  return "ASR_INIT_FAIL";
    case VK_ERR_USB_FAIL:       return "USB_FAIL";
    case VK_ERR_NVS_FAIL:       return "NVS_FAIL";
    case VK_ERR_WDT_RESET:      return "WDT_RESET";
    case VK_ERR_NONE:
    default:                    return "NONE";
    }
}

const char *vk_warn_str(vk_warn_t w)
{
    switch (w) {
    case VK_WARN_ASR_UNAVAILABLE: return "ASR_UNAVAILABLE";
    case VK_WARN_RSSI_LOW:        return "RSSI_LOW";
    case VK_WARN_MIC_DEGRADED:    return "MIC_DEGRADED";
    case VK_WARN_NONE:
    default:                      return "NONE";
    }
}

const char *vk_asr_str(vk_asr_t a)
{
    switch (a) {
    case VK_ASR_OK:   return "OK";
    case VK_ASR_BUSY: return "BUSY";
    case VK_ASR_DOWN: return "DOWN";
    case VK_ASR_UNKNOWN:
    default:          return "";
    }
}

void vk_status_init(vk_status_t *st)
{
    if (st == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->state = VK_STATE_BOOT;
    st->err   = VK_ERR_NONE;
    st->warn  = VK_WARN_NONE;
    st->asr   = VK_ASR_UNKNOWN;
}

/* ------------------------- 内部追加辅助函数 ----------------------------- */

static void app_str(char *out, size_t cap, size_t *pos, const char *s)
{
    size_t p = *pos;
    if (s == NULL) {
        s = "";
    }
    while ((*s != '\0') && ((p + 1u) < cap)) {
        out[p++] = *s++;
    }
    out[p] = '\0';
    *pos = p;
}

static void app_kv(char *out, size_t cap, size_t *pos, const char *k, const char *v)
{
    if ((v == NULL) || (v[0] == '\0')) {
        return;   /* 空值的字段直接省略 */
    }
    if (*pos > 0u) {
        app_str(out, cap, pos, ",");
    }
    app_str(out, cap, pos, k);
    app_str(out, cap, pos, "=");
    app_str(out, cap, pos, v);
}

static void app_kv_int(char *out, size_t cap, size_t *pos, const char *k, long v, int valid)
{
    char tmp[24];
    if (valid == 0) {
        return;
    }
    (void)snprintf(tmp, sizeof(tmp), "%ld", v);
    app_kv(out, cap, pos, k, tmp);
}

int vk_status_format(const vk_status_t *st, char *out, size_t cap)
{
    size_t pos = 0u;

    if ((st == NULL) || (out == NULL) || (cap == 0u)) {
        return -1;
    }
    out[0] = '\0';

    app_str(out, cap, &pos, vk_state_str(st->state));
    app_kv(out, cap, &pos, "FW", st->fw);
    app_kv(out, cap, &pos, "SSID", st->ssid);
    app_kv(out, cap, &pos, "IP", st->ip);
    app_kv_int(out, cap, &pos, "RSSI", (long)st->rssi, (st->rssi != 0) ? 1 : 0);
    if (st->err != VK_ERR_NONE) {
        app_kv(out, cap, &pos, "ERR", vk_err_str(st->err));
    }
    if (st->warn != VK_WARN_NONE) {
        app_kv(out, cap, &pos, "WARN", vk_warn_str(st->warn));
    }
    app_kv(out, cap, &pos, "ASR", vk_asr_str(st->asr));

    return (int)pos;
}

int vk_status_format_uart1(const vk_status_t *st, char *out, size_t cap)
{
    int n;
    size_t pos = 0u;

    if ((st == NULL) || (out == NULL) || (cap == 0u)) {
        return -1;
    }
    app_str(out, cap, &pos, "STATUS:");
    n = vk_status_format(st, &out[pos], cap - pos);
    if (n < 0) {
        return -1;
    }
    pos += (size_t)n;
    app_str(out, cap, &pos, "\r\n");
    return (int)pos;
}
