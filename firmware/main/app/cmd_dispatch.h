/**
 * @file cmd_dispatch.h
 * @brief 下行指令分发：WiFi 配置 / ASR 配置 / 进入烧录 / 语音启停 / 状态请求。
 *
 * 冻结依据：docs/00-技术设计文档.md §7.1（烧录态唯一入口）
 *
 * 设计要点：本模块是**纯逻辑**的——它只做校验和解析，把结果写进自己的结构体，
 * 并通过返回值告诉 app.c "该执行什么动作"。真正的副作用（写 NVS、esp_restart）
 * 由 app.c 通过 vk_port 完成。这样整套安全规则可以在 PC 上完整单测。
 */
#ifndef VIBEKEY_CMD_DISPATCH_H
#define VIBEKEY_CMD_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#include "key_fsm.h"   /* 按键字符表，默认语音键 */
#include "port.h"      /* VK_ASR_* 宏（与 port 层共用） */
#include "proto.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------ ASR 配置 ------------------------------- */
/* 宏已移到 port.h：ASR 是 port 层也需要的常量，不能只放在 app 层头里 */

typedef struct {
    char     provider[VK_ASR_PROVIDER_MAX];   /**< "tencent" / "volcano" */
    char     appid[VK_ASR_APPID_MAX];
    char     secret_id[VK_ASR_SECRET_MAX];
    char     secret_key[VK_ASR_SECRET_MAX];
    uint32_t sample_rate;                     /**< 16000 */
    char     format[VK_ASR_FORMAT_MAX];       /**< "pcm" */
    uint8_t  channels;                        /**< 1 */
    char     voice_key;                       /**< 哪颗键是语音键，'7' */
} vk_asr_cfg_t;

/* ------------------------------ 动作 ----------------------------------- */
typedef enum {
    VK_CMD_ACT_NONE = 0,
    VK_CMD_ACT_WIFI_SAVE,     /**< 参数在 d->wifi_ssid / d->wifi_pass */
    VK_CMD_ACT_ASR_SAVE,      /**< 参数在 d->asr_cfg */
    VK_CMD_ACT_ENTER_DFU,     /**< 已通过全部安全校验，可以重启进下载模式 */
    VK_CMD_ACT_VOICE_START,
    VK_CMD_ACT_VOICE_STOP,
    VK_CMD_ACT_STATUS_REQ,
    VK_CMD_ACT_PONG,          /**< 上位机主动 ping，需回 0x04 pong */
} vk_cmd_action_t;

/* ------------------------------ 处理结果 ------------------------------- */
typedef enum {
    VK_CMD_RES_OK = 0,        /**< 已接受，应回 ACK */
    VK_CMD_RES_NACK,          /**< 拒绝，应回 NACK，原因见 ack 载荷 */
    VK_CMD_RES_IGNORED        /**< 静默丢弃（重放等），不回 ACK */
} vk_cmd_result_t;

/* NACK 原因码（ASCII） */
#define VK_CMD_RSN_TYPE    "TYPE"     /**< 未知帧类型 */
#define VK_CMD_RSN_CH      "CH"       /**< 通道不允许（如 WiFi 来的烧录指令） */
#define VK_CMD_RSN_MAGIC   "MAGIC"    /**< magic word 不匹配 */
#define VK_CMD_RSN_STATE   "STATE"    /**< 当前状态不允许（如 ERROR 态进 DFU） */
#define VK_CMD_RSN_LEN     "LEN"      /**< 载荷长度非法 */
#define VK_CMD_RSN_FMT     "FMT"      /**< 载荷格式非法（缺字段 / 含非法字符） */
#define VK_CMD_RSN_REPLAY  "REPLAY"   /**< 重放，已忽略 */

/* ------------------------------ 分发器 --------------------------------- */
typedef struct {
    /* 配置 */
    uint32_t dfu_window_ms;        /**< magic 一次性有效窗口，默认 5000 */

    /* 运行状态 */
    vk_state_t state;              /**< 当前设备状态，app.c 每拍同步 */
    uint8_t    dfu_consumed;       /**< magic 是否已被接受过一次 */
    uint32_t   dfu_consumed_at_ms;

    /* 解析结果（app.c 据此执行副作用） */
    char       wifi_ssid[33];
    char       wifi_pass[65];
    vk_asr_cfg_t asr_cfg;

    /* 云连接配置（通过 0x12 ASR 配置帧的扩展键下发，复用协议不加新帧类型） */
    char       cloud_url[128];   /**< wss://<域名>/ws/device */
    char       cloud_token[64];  /**< device_token，设备连云时认证用 */
} vk_cmd_dispatch_t;

/** 初始化。window_ms 传 0 用默认值 5000。 */
void vk_cmd_init(vk_cmd_dispatch_t *d, uint32_t window_ms);

/** 让分发器知道当前设备状态（每拍或状态切换时调用）。 */
void vk_cmd_set_state(vk_cmd_dispatch_t *d, vk_state_t state);

/**
 * 处理一个下行帧。
 *
 * @param f        已解码的帧
 * @param channel  来源通道 VK_CH_USB / VK_CH_WIFI
 * @param now_ms   当前时间戳
 * @param ack_out  ACK/NACK 载荷输出缓冲，可为 NULL
 * @param ack_cap  ack_out 容量
 * @param ack_len  出参：实际写入的 ACK 载荷长度
 * @return vk_cmd_action_t（本次要求 app.c 执行的动作）
 */
vk_cmd_action_t vk_cmd_handle(vk_cmd_dispatch_t *d, const proto_frame_t *f, uint8_t channel,
                              uint32_t now_ms,
                              uint8_t *ack_out, size_t ack_cap, size_t *ack_len);

/* --------------------------- 载荷解析（独立可测） ----------------------- */

/**
 * 解析 0x11 帧载荷 "SSID\0PASSWORD\0"。
 * 约束：SSID 1..32 字节，密码 8..64 字节；不得含 ',' 或 '='（会破坏状态字符串）。
 * @return 0 成功；-1 长度非法；-2 格式非法；-3 含非法字符
 */
int vk_cmd_parse_wifi(const uint8_t *payload, uint16_t len,
                      char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

/**
 * 解析 0x12 帧载荷 "K=V\0K=V\0...\0"。
 * 必填键：provider / appid / secret_id / secret_key。
 * @return 0 成功；-1 缺必填键；-2 单个键值对格式非法；-3 值超长
 */
int vk_cmd_parse_asr(const uint8_t *payload, uint16_t len, vk_asr_cfg_t *cfg);

/** 用默认值填充 ASR 配置（16kHz / pcm / 单声道 / voice_key='7'）。 */
void vk_asr_cfg_default(vk_asr_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_CMD_DISPATCH_H */
