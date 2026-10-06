/**
 * @file asr_fsm.c
 * @brief 语音会话状态机实现。
 */
#include "asr_fsm.h"
#include <stddef.h>   /* NULL */

void vk_asr_init(vk_asr_fsm_t *f, vk_asr_mode_t mode)
{
    if (f == NULL) {
        return;
    }
    f->mode = mode;
    f->st = VK_ASR_ST_IDLE;
    f->warn_set = 0u;
}

vk_asr_state_t vk_asr_state(const vk_asr_fsm_t *f)
{
    return (f == NULL) ? VK_ASR_ST_IDLE : f->st;
}

const char *vk_asr_state_str(vk_asr_state_t s)
{
    switch (s) {
    case VK_ASR_ST_IDLE:       return "IDLE";
    case VK_ASR_ST_CAPTURING:  return "CAPTURING";
    case VK_ASR_ST_UPLOADING:  return "UPLOADING";
    case VK_ASR_ST_ERROR:      return "ERROR";
    default:                   return "?";
    }
}

const char *vk_asr_mode_str(vk_asr_mode_t m)
{
    switch (m) {
    case VK_ASR_MODE_STREAM:  return "STREAM";
    case VK_ASR_MODE_ONESHOT: return "ONESHOT";
    default:                  return "?";
    }
}

void vk_asr_clear_error(vk_asr_fsm_t *f)
{
    if (f != NULL) {
        f->st = VK_ASR_ST_IDLE;
    }
}

vk_asr_action_t vk_asr_event(vk_asr_fsm_t *f, vk_asr_event_t ev)
{
    if (f == NULL) {
        return VK_ASR_ACT_NONE;
    }

    switch (f->st) {

    case VK_ASR_ST_IDLE:
        if (ev == VK_ASR_EV_VOICE_DOWN) {
            /* 两种模式都是"按下开始" */
            f->st = VK_ASR_ST_CAPTURING;
            return VK_ASR_ACT_MIC_START;
        }
        if (ev == VK_ASR_EV_ASR_FAIL) {
            /* 没有会话时也报告语音不可用（PRD 5.6：无外网 / Key 失效）。
             * 只告警，不进 ERROR 态，LED 不变。 */
            f->warn_set = 1u;
            return VK_ASR_ACT_WARN_UNAVAIL;
        }
        return VK_ASR_ACT_NONE;

    case VK_ASR_ST_CAPTURING:
        switch (ev) {
        case VK_ASR_EV_VOICE_UP:
            if (f->mode == VK_ASR_MODE_STREAM) {
                /* 流式：松手结束 */
                f->st = VK_ASR_ST_UPLOADING;
                return VK_ASR_ACT_MIC_STOP;
            }
            /* 整包模式忽略松开 */
            return VK_ASR_ACT_NONE;

        case VK_ASR_EV_VOICE_DOWN:
            if (f->mode == VK_ASR_MODE_ONESHOT) {
                /* 整包：再按一下结束 */
                f->st = VK_ASR_ST_UPLOADING;
                return VK_ASR_ACT_MIC_STOP;
            }
            /* 流式模式忽略重复按下 */
            return VK_ASR_ACT_NONE;

        case VK_ASR_EV_MIC_ERROR:
            /* 麦克风是硬件故障 -> ERROR, ERR=MIC_FAIL */
            f->st = VK_ASR_ST_ERROR;
            return VK_ASR_ACT_ERROR_MIC;

        case VK_ASR_EV_ASR_FAIL:
            /* 流式过程中 ASR 挂掉：不算故障，只警告 */
            f->warn_set = 1u;
            return VK_ASR_ACT_WARN_UNAVAIL;

        default:
            return VK_ASR_ACT_NONE;
        }

    case VK_ASR_ST_UPLOADING:
        switch (ev) {
        case VK_ASR_EV_ASR_DONE:
            f->st = VK_ASR_ST_IDLE;
            return VK_ASR_ACT_NONE;

        case VK_ASR_EV_ASR_FAIL:
            /* 关键：不进 ERROR 态，LED 不变，只上报警告 */
            f->st = VK_ASR_ST_IDLE;
            f->warn_set = 1u;
            return VK_ASR_ACT_WARN_UNAVAIL;

        case VK_ASR_EV_VOICE_DOWN:
            /* 上传期间又按语音键：忽略，避免打断上传 */
            return VK_ASR_ACT_NONE;

        default:
            return VK_ASR_ACT_NONE;
        }

    case VK_ASR_ST_ERROR:
    default:
        /* 错误态下忽略一切事件，直到 app 调 vk_asr_clear_error() */
        return VK_ASR_ACT_NONE;
    }
}
