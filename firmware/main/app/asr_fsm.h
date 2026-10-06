/**
 * @file asr_fsm.h
 * @brief 语音会话状态机。
 *
 * 冻结依据：docs/00-技术设计文档.md §5.5
 *
 * 两种交互模式（PRD 5.6）：
 *   流式  按住说话，松开结束。需要释放沿 -> 依赖 0x05 释放帧（设计文档 §5.4）
 *   整包  点一下开始录，再点一下结束
 *
 * 关键约束：ASR 失败**不进入 ERROR 态**（语音是附加功能，WiFi 配网仍正常），
 * 只上报 WARN=ASR_UNAVAILABLE，LED 不变。麦克风初始化失败才是 ERROR, ERR=MIC_FAIL。
 */
#ifndef VIBEKEY_ASR_FSM_H
#define VIBEKEY_ASR_FSM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VK_ASR_MODE_STREAM = 0,    /**< 流式：按住说话 */
    VK_ASR_MODE_ONESHOT        /**< 整包：点按切换 */
} vk_asr_mode_t;

typedef enum {
    VK_ASR_ST_IDLE = 0,
    VK_ASR_ST_CAPTURING,
    VK_ASR_ST_UPLOADING,
    VK_ASR_ST_ERROR            /**< 仅麦克风硬件故障会进入 */
} vk_asr_state_t;

typedef enum {
    VK_ASR_EV_VOICE_DOWN = 0,  /**< 语音键按下，或上位机 0x14 start */
    VK_ASR_EV_VOICE_UP,        /**< 语音键松开，或上位机 0x14 stop */
    VK_ASR_EV_MIC_ERROR,       /**< 麦克风采集/初始化失败 */
    VK_ASR_EV_ASR_FAIL,        /**< ASR 服务不可达 / Key 失效 / 无外网 */
    VK_ASR_EV_ASR_DONE,        /**< ASR 返回结果（文本已由 port 层发出） */
} vk_asr_event_t;

typedef enum {
    VK_ASR_ACT_NONE = 0,
    VK_ASR_ACT_MIC_START,      /**< app 调用 port->asr_start() */
    VK_ASR_ACT_MIC_STOP,       /**< app 调用 port->asr_stop() */
    VK_ASR_ACT_WARN_UNAVAIL,   /**< app 置 WARN=ASR_UNAVAILABLE 并上报 */
    VK_ASR_ACT_ERROR_MIC       /**< app 进入 ERROR 态，ERR=MIC_FAIL */
} vk_asr_action_t;

typedef struct {
    vk_asr_mode_t  mode;
    vk_asr_state_t st;
    uint8_t        warn_set;    /**< 当前是否已置 ASR_UNAVAILABLE 警告 */
} vk_asr_fsm_t;

void vk_asr_init(vk_asr_fsm_t *f, vk_asr_mode_t mode);
vk_asr_state_t vk_asr_state(const vk_asr_fsm_t *f);
const char *vk_asr_state_str(vk_asr_state_t s);
const char *vk_asr_mode_str(vk_asr_mode_t m);

/** 喂入一个事件，返回 app 应执行的动作。 */
vk_asr_action_t vk_asr_event(vk_asr_fsm_t *f, vk_asr_event_t ev);

/** 清错误态（上报完 ERROR 后由 app 调用，避免卡死）。 */
void vk_asr_clear_error(vk_asr_fsm_t *f);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_ASR_FSM_H */
