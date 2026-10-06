/**
 * @file app.h
 * @brief 应用层主调度器：把各状态机、协议编解码与移植层缝在一起。
 */
#ifndef VIBEKEY_APP_H
#define VIBEKEY_APP_H

#include <stdint.h>

#include "asr_fsm.h"
#include "cmd_dispatch.h"
#include "key_fsm.h"
#include "net_fsm.h"
#include "proto.h"
#include "seq.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VK_APP_HEARTBEAT_MS 1000u   /**< 状态主动上报周期（PRD 6.2 建议 1s） */
#define VK_APP_TICK_MS      VK_SAMPLE_MS

typedef struct {
    /* 各状态机 */
    vk_key_fsm_t      keys;
    vk_net_fsm_t      net;
    vk_asr_fsm_t      asr;
    vk_cmd_dispatch_t cmd;
    vk_seq_t          seq;

    /* 通道字节流 */
    proto_stream_t    usb_stream;
    proto_stream_t    wifi_stream;

    /* 当前设备状态 */
    vk_state_t        state;
    vk_state_t        prev_state;
    vk_status_t       status;
    uint8_t           status_dirty;   /**< 需要主动上报 */

    /* 时序 */
    uint32_t          last_hb_ms;
    uint32_t          last_tick_ms;
    uint32_t          boot_ms;

    /* 运行期配置 */
    char              mac4[8];        /**< MAC 后 4 位，如 "1A2B" */
    uint8_t           release_enabled;
    vk_asr_mode_t     asr_mode;
    uint8_t           inited;
} vk_app_t;

/** 初始化（绑定 vk_port 之后调用）。 */
void vk_app_init(vk_app_t *app);

/** 主循环一轮。返回 0 正常，负值表示严重错误。 */
int vk_app_poll(vk_app_t *app);

/** 移植层收到 USB 字节后调用。 */
void vk_app_on_usb_bytes(vk_app_t *app, const uint8_t *data, uint32_t len);

/** 移植层收到 WiFi TCP 字节后调用。 */
void vk_app_on_wifi_bytes(vk_app_t *app, const uint8_t *data, uint32_t len);

/** 移植层收到一个完整下行帧后调用（USB 或 WiFi）。 */
void vk_app_on_frame(vk_app_t *app, const proto_frame_t *f, uint8_t channel);

/** 请求切到指定状态（内部会做合法性与 LED 同步）。 */
void vk_app_set_state(vk_app_t *app, vk_state_t st);

/** 当前状态。 */
vk_state_t vk_app_state(const vk_app_t *app);

/** ASR 服务返回文本后调用（port 层解析完 ASR 响应时）。is_final=1 表示最终结果。 */
void vk_app_on_asr_text(vk_app_t *app, const char *utf8_text, uint8_t is_final);

/** ASR 出错时调用。 */
void vk_app_on_asr_fail(vk_app_t *app);

/** 麦克风出错时调用。 */
void vk_app_on_mic_error(vk_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_APP_H */
