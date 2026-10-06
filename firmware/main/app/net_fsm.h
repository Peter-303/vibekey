/**
 * @file net_fsm.h
 * @brief WiFi 配网状态机：AP / STA 切换、NVS 凭据、重试与回退。
 *
 * 冻结依据：docs/00-技术设计文档.md §5.2
 *
 * 同样是纯逻辑：只做状态迁移，通过返回值告诉 app.c 该调用哪个 port 函数。
 */
#ifndef VIBEKEY_NET_FSM_H
#define VIBEKEY_NET_FSM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 冻结参数（PRD 5.2 建议值）。重试为背靠背进行，总耗时约 3x10s = 30s，
 * 落在 PRD "约 40s 内回到红蓝交替闪烁" 的验收口径内。 */
#define VK_NET_STA_TIMEOUT_MS  10000u   /**< STA 单次尝试超时 */
#define VK_NET_STA_RETRY_MAX   3u       /**< 重试次数上限 */
#define VK_NET_SSID_MAX        32u
#define VK_NET_PASS_MAX        64u

typedef enum {
    VK_NET_ST_INIT = 0,          /**< 刚上电，尚未读取 NVS */
    VK_NET_ST_STA_CONNECTING,    /**< 正在尝试连接路由器 */
    VK_NET_ST_CONNECTED,         /**< STA 已连接并取得 IP */
    VK_NET_ST_AP                 /**< AP 配网模式 */
} vk_net_state_t;

typedef enum {
    VK_NET_ACT_NONE = 0,
    VK_NET_ACT_START_AP,         /**< 参数在 f->ap_ssid / f->cfg.ap_pass */
    VK_NET_ACT_START_STA,        /**< 参数在 f->ssid / f->pass */
    VK_NET_ACT_SAVE_CREDS,       /**< 参数在 f->ssid / f->pass，app 需写 NVS 并 commit */
    VK_NET_ACT_REBOOT,           /**< NVS 已提交，可以重启 */
} vk_net_action_t;

typedef struct {
    uint32_t sta_timeout_ms;
    uint8_t  sta_retry_max;
    char     ap_pass[VK_NET_PASS_MAX + 1u];
} vk_net_cfg_t;

typedef struct {
    vk_net_cfg_t  cfg;
    vk_net_state_t st;
    uint8_t       retry_count;
    uint32_t      state_since_ms;
    uint32_t      last_attempt_ms;

    char          ap_ssid[VK_NET_SSID_MAX + 1u];  /**< "VibeKey-" + MAC 后 4 位 */
    char          ssid[VK_NET_SSID_MAX + 1u];
    char          pass[VK_NET_PASS_MAX + 1u];
    char          ip[16];
    int           rssi;
} vk_net_fsm_t;

/**
 * 初始化。
 * @param cfg   传 NULL 用冻结默认值
 * @param mac4  设备 MAC 后 4 位十六进制大写字符串（如 "1A2B"），用于拼 AP SSID
 */
void vk_net_init(vk_net_fsm_t *f, const vk_net_cfg_t *cfg, const char *mac4);

/** 取默认配置（含 AP 密码占位值，量产前必须替换，见设计文档 §7.2）。 */
vk_net_cfg_t vk_net_default_cfg(void);

/** 当前状态。 */
vk_net_state_t vk_net_state(const vk_net_fsm_t *f);

/** 状态是否等价于"设备认为自己正常"（STA_CONNECTING / CONNECTED）。 */
int vk_net_is_up(const vk_net_fsm_t *f);

/**
 * 上电后 app 从 NVS 读到凭据（或没读到）时调用。
 * @return VK_NET_ACT_START_STA（有凭据）或 VK_NET_ACT_START_AP（无凭据）
 */
vk_net_action_t vk_net_on_creds(vk_net_fsm_t *f, uint8_t have_creds,
                                const char *ssid, const char *pass,
                                uint32_t now_ms);

/**
 * 主循环每拍调用。
 * @param sta_up 1 = STA 已关联且取得 IP
 * @param ip     当前 IP 字符串，可为 NULL
 * @param rssi   当前信号强度，0 = 无效
 * @return 需要 app 执行的动作
 */
vk_net_action_t vk_net_tick(vk_net_fsm_t *f, uint32_t now_ms, uint8_t sta_up,
                            const char *ip, int rssi);

/**
 * 上位机下发新 WiFi 配置（cmd_dispatch 解析成功后由 app 转来）。
 * @return VK_NET_ACT_SAVE_CREDS；随后 app 写 NVS 并 commit，再调 vk_net_on_saved()
 */
vk_net_action_t vk_net_new_config(vk_net_fsm_t *f, const char *ssid,
                                  const char *pass, uint32_t now_ms);

/** app 完成 NVS 写入并 commit 成功后调用。@return VK_NET_ACT_REBOOT */
vk_net_action_t vk_net_on_saved(vk_net_fsm_t *f, uint32_t now_ms);

const char *vk_net_state_str(vk_net_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_NET_FSM_H */
