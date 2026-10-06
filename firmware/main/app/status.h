/**
 * @file status.h
 * @brief 设备状态码 / 故障码 / 警告码定义与上报字符串格式化。
 *
 * 冻结依据：docs/00-技术设计文档.md §5.1、§3.5
 *
 * 关键设计：状态码与警告码分离。
 *   - 状态码（7 个）驱动 LED，且必须与上位机心跳显示一致；
 *   - 警告码只是上报字段，完全不改变 LED。
 * 这解决了 PRD 5.1（灯与心跳一致）与 5.6（ASR 不可用时只报 WARN）的矛盾。
 */
#ifndef VIBEKEY_STATUS_H
#define VIBEKEY_STATUS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------ 状态码 --------------------------------- */
typedef enum {
    VK_STATE_BOOT = 0x01,         /**< 上电初始化，蓝+红常亮 */
    VK_STATE_AP_START = 0x02,     /**< 热点配网模式，红蓝交替闪 */
    VK_STATE_STA_JOIN_OK = 0x03,  /**< STA 关联成功并拿到 IP，蓝常亮 */
    VK_STATE_STA_JOIN_FAIL = 0x04,/**< STA 单次失败（瞬态），红灯慢闪 */
    VK_STATE_WORK_READY = 0x05,   /**< 正常工作，蓝常亮 */
    VK_STATE_ERROR = 0x06,        /**< 不可恢复故障，红常亮 */
    VK_STATE_DOWNLOAD = 0x07      /**< 即将进入下载模式，灯由 bootloader 决定 */
} vk_state_t;

/* ------------------------------ 故障码 --------------------------------- */
typedef enum {
    VK_ERR_NONE = 0,
    VK_ERR_WIFI_FAIL,
    VK_ERR_MIC_FAIL,
    VK_ERR_ASR_INIT_FAIL,
    VK_ERR_USB_FAIL,
    VK_ERR_NVS_FAIL,
    VK_ERR_WDT_RESET
} vk_err_t;

/* ------------------------------ 警告码 --------------------------------- */
typedef enum {
    VK_WARN_NONE = 0,
    VK_WARN_ASR_UNAVAILABLE,   /**< ASR 不可达 / Key 失效 / 无外网；灯不变 */
    VK_WARN_RSSI_LOW,          /**< RSSI < -80 dBm */
    VK_WARN_MIC_DEGRADED       /**< 采集异常（长期静音 / 底噪异常） */
} vk_warn_t;

/* ------------------------------ ASR 状态 -------------------------------- */
typedef enum {
    VK_ASR_UNKNOWN = 0,
    VK_ASR_OK,
    VK_ASR_BUSY,
    VK_ASR_DOWN
} vk_asr_t;

/* --------------------------- 状态快照结构 ------------------------------- */
typedef struct {
    vk_state_t state;
    vk_err_t   err;
    vk_warn_t  warn;
    vk_asr_t   asr;
    char       fw[16];    /**< 固件版本，如 "1.0.0" */
    char       ip[16];    /**< STA 模式下有效，空字符串表示无 */
    char       ssid[33];  /**< AP 模式下为自身热点名；STA 模式下为所连路由 */
    int        rssi;      /**< 0 表示无效 */
} vk_status_t;

/* -------------------------------- API ---------------------------------- */
const char *vk_state_str(vk_state_t s);
const char *vk_err_str(vk_err_t e);
const char *vk_warn_str(vk_warn_t w);
const char *vk_asr_str(vk_asr_t a);

/** 用默认值初始化快照（state=BOOT，其余空/无）。 */
void vk_status_init(vk_status_t *st);

/**
 * 格式化为 "<STATE>[,K=V]*"，例如：
 *   WORK_READY,FW=1.0.0,RSSI=-58,ASR=OK
 *   AP_START,SSID=VibeKey-1A2B,FW=1.0.0
 *   WORK_READY,WARN=ASR_UNAVAILABLE,FW=1.0.0
 *
 * @return 写入的字符数（不含结尾 NUL）；cap 不足时返回截断后的长度，仍保证 NUL 结尾
 */
int vk_status_format(const vk_status_t *st, char *out, size_t cap);

/**
 * 格式化为 UART1 调试行 "STATUS:<上者>\r\n"。
 * 与 USB/WiFi 的 0x02 载荷共用同一个 vk_status_format()，保证 PRD 要求的
 * "与 UART1 一致"。
 */
int vk_status_format_uart1(const vk_status_t *st, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_STATUS_H */
