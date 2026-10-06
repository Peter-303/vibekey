/**
 * @file test_asr_fsm.c
 * @brief 语音会话状态机测试。对应 PRD 5.6 与设计文档 §5.5。
 */
#include "asr_fsm.h"
#include "test_util.h"

VK_SUITE(asr_stream_mode)
{
    vk_asr_fsm_t f;
    vk_asr_init(&f, VK_ASR_MODE_STREAM);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_IDLE);

    /* 按住 -> 开始采集 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_MIC_START);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_CAPTURING);

    /* 流式模式下重复按下应忽略 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_CAPTURING);

    /* 松开 -> 停止采集，进入上传 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_UP), VK_ASR_ACT_MIC_STOP);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_UPLOADING);

    /* ASR 成功 -> 回 IDLE */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_ASR_DONE), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_IDLE);
}

VK_SUITE(asr_oneshot_mode)
{
    vk_asr_fsm_t f;
    vk_asr_init(&f, VK_ASR_MODE_ONESHOT);

    /* 点一下开始 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_MIC_START);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_CAPTURING);

    /* 整包模式忽略松开 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_UP), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_CAPTURING);

    /* 再点一下结束 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_MIC_STOP);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_UPLOADING);
}

/* 关键：ASR 失败不进 ERROR 态，LED 不变 */
VK_SUITE(asr_fail_is_not_error)
{
    vk_asr_fsm_t f;
    vk_asr_init(&f, VK_ASR_MODE_STREAM);
    (void)vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN);
    (void)vk_asr_event(&f, VK_ASR_EV_VOICE_UP);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_UPLOADING);

    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_ASR_FAIL), VK_ASR_ACT_WARN_UNAVAIL);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_IDLE);
    VK_CHECK_EQ_INT(f.warn_set, 1);

    /* 采集过程中 ASR 挂掉：同样只警告，不影响采集 */
    vk_asr_init(&f, VK_ASR_MODE_STREAM);
    (void)vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN);
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_ASR_FAIL), VK_ASR_ACT_WARN_UNAVAIL);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_CAPTURING);
}

/* 麦克风是硬件故障，必须进 ERROR 态 */
VK_SUITE(asr_mic_error_is_error)
{
    vk_asr_fsm_t f;
    vk_asr_init(&f, VK_ASR_MODE_STREAM);
    (void)vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN);
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_MIC_ERROR), VK_ASR_ACT_ERROR_MIC);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_ERROR);

    /* 错误态下忽略一切事件 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_ASR_DONE), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_ERROR);

    /* app 上报完 ERROR 后清错 */
    vk_asr_clear_error(&f);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_IDLE);
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_MIC_START);
}

VK_SUITE(asr_idle_noise)
{
    vk_asr_fsm_t f;
    vk_asr_init(&f, VK_ASR_MODE_STREAM);
    /* IDLE 态收到松开应忽略 */
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_UP), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_IDLE);
    /* 上传期间再按语音键应忽略，避免打断上传 */
    (void)vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN);
    (void)vk_asr_event(&f, VK_ASR_EV_VOICE_UP);
    VK_CHECK_EQ_INT(vk_asr_event(&f, VK_ASR_EV_VOICE_DOWN), VK_ASR_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_ASR_ST_UPLOADING);
}

VK_SUITE(asr_strings)
{
    VK_CHECK_EQ_STR(vk_asr_state_str(VK_ASR_ST_IDLE), "IDLE");
    VK_CHECK_EQ_STR(vk_asr_state_str(VK_ASR_ST_CAPTURING), "CAPTURING");
    VK_CHECK_EQ_STR(vk_asr_state_str(VK_ASR_ST_UPLOADING), "UPLOADING");
    VK_CHECK_EQ_STR(vk_asr_state_str(VK_ASR_ST_ERROR), "ERROR");
    VK_CHECK_EQ_STR(vk_asr_mode_str(VK_ASR_MODE_STREAM), "STREAM");
    VK_CHECK_EQ_STR(vk_asr_mode_str(VK_ASR_MODE_ONESHOT), "ONESHOT");
}

void test_asr_fsm_run(void)
{
    vk_suite_name = "asr_fsm";
    (void)printf("  [asr_fsm] 流式/整包 / ASR失败不降级为故障 / 麦克风故障进ERROR\n");
    suite_asr_stream_mode();
    suite_asr_oneshot_mode();
    suite_asr_fail_is_not_error();
    suite_asr_mic_error_is_error();
    suite_asr_idle_noise();
    suite_asr_strings();
}
