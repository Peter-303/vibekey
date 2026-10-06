/**
 * @file test_led_fsm.c
 * @brief LED 状态机测试，逐档对应 PRD 5.1 的 LED 状态表。
 */
#include "led_fsm.h"
#include "test_util.h"

VK_SUITE(led_states)
{
    vk_led_output_t o;

    /* 上电初始化：蓝常亮 + 红常亮 */
    o = vk_led_output(VK_STATE_BOOT, 0u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    VK_CHECK_EQ_INT(o.red_on, 1);
    o = vk_led_output(VK_STATE_BOOT, 999u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    VK_CHECK_EQ_INT(o.red_on, 1);

    /* 正常工作：蓝常亮、红熄灭 */
    o = vk_led_output(VK_STATE_WORK_READY, 0u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    VK_CHECK_EQ_INT(o.red_on, 0);
    o = vk_led_output(VK_STATE_WORK_READY, 12345u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    VK_CHECK_EQ_INT(o.red_on, 0);

    /* STA 已连接：同上 */
    o = vk_led_output(VK_STATE_STA_JOIN_OK, 777u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    VK_CHECK_EQ_INT(o.red_on, 0);

    /* 故障：蓝熄灭、红常亮 */
    o = vk_led_output(VK_STATE_ERROR, 0u);
    VK_CHECK_EQ_INT(o.blue_on, 0);
    VK_CHECK_EQ_INT(o.red_on, 1);
    o = vk_led_output(VK_STATE_ERROR, 99999u);
    VK_CHECK_EQ_INT(o.blue_on, 0);
    VK_CHECK_EQ_INT(o.red_on, 1);

    /* 烧录态：固件不定义 -> 全灭 */
    o = vk_led_output(VK_STATE_DOWNLOAD, 0u);
    VK_CHECK_EQ_INT(o.blue_on, 0);
    VK_CHECK_EQ_INT(o.red_on, 0);
}

VK_SUITE(led_ap_alternate)
{
    vk_led_output_t o;
    uint32_t t;

    /* 红蓝交替：同频率、互斥亮灭。周期 1000ms，前 500ms 蓝，后 500ms 红。*/
    for (t = 0u; t < 500u; t += 37u) {
        o = vk_led_output(VK_STATE_AP_START, t);
        VK_CHECK_EQ_INT(o.blue_on, 1);
        VK_CHECK_EQ_INT(o.red_on, 0);
    }
    for (t = 500u; t < 1000u; t += 37u) {
        o = vk_led_output(VK_STATE_AP_START, t);
        VK_CHECK_EQ_INT(o.blue_on, 0);
        VK_CHECK_EQ_INT(o.red_on, 1);
    }
    /* 周期边界 */
    o = vk_led_output(VK_STATE_AP_START, 499u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    o = vk_led_output(VK_STATE_AP_START, 500u);
    VK_CHECK_EQ_INT(o.red_on, 1);
    /* 回绕后仍应交替（1000 和 1600 都远离 500ms 边界） */
    o = vk_led_output(VK_STATE_AP_START, 1000u);
    VK_CHECK_EQ_INT(o.blue_on, 1);
    VK_CHECK_EQ_INT(o.red_on, 0);
    o = vk_led_output(VK_STATE_AP_START, 1600u);
    VK_CHECK_EQ_INT(o.red_on, 1);
    VK_CHECK_EQ_INT(o.blue_on, 0);
}

VK_SUITE(led_fail_blink)
{
    vk_led_output_t o;
    /* STA 单次失败：红灯慢闪 */
    o = vk_led_output(VK_STATE_STA_JOIN_FAIL, 100u);
    VK_CHECK_EQ_INT(o.red_on, 1);
    VK_CHECK_EQ_INT(o.blue_on, 0);
    o = vk_led_output(VK_STATE_STA_JOIN_FAIL, 700u);
    VK_CHECK_EQ_INT(o.red_on, 0);
}

VK_SUITE(led_pattern_table)
{
    vk_led_pattern_t p;

    p = vk_led_pattern_for(VK_STATE_BOOT);
    VK_CHECK_EQ_INT(p.blue, VK_LED_MODE_ON);
    VK_CHECK_EQ_INT(p.red, VK_LED_MODE_ON);

    p = vk_led_pattern_for(VK_STATE_AP_START);
    VK_CHECK_EQ_INT(p.blue, VK_LED_MODE_BLINK);
    VK_CHECK_EQ_INT(p.red, VK_LED_MODE_BLINK);

    p = vk_led_pattern_for(VK_STATE_WORK_READY);
    VK_CHECK_EQ_INT(p.blue, VK_LED_MODE_ON);
    VK_CHECK_EQ_INT(p.red, VK_LED_MODE_OFF);

    p = vk_led_pattern_for(VK_STATE_ERROR);
    VK_CHECK_EQ_INT(p.blue, VK_LED_MODE_OFF);
    VK_CHECK_EQ_INT(p.red, VK_LED_MODE_ON);

    p = vk_led_pattern_for(VK_STATE_DOWNLOAD);
    VK_CHECK_EQ_INT(p.blue, VK_LED_MODE_OFF);
    VK_CHECK_EQ_INT(p.red, VK_LED_MODE_OFF);

    VK_CHECK_EQ_INT(VK_LED_BLINK_PERIOD_MS, 1000);
    VK_CHECK_EQ_INT(VK_LED_BLINK_HALF_MS, 500);
}

VK_SUITE(led_describe)
{
    VK_CHECK_EQ_STR(vk_led_describe(VK_STATE_BOOT), "blue=ON red=ON");
    VK_CHECK_EQ_STR(vk_led_describe(VK_STATE_AP_START), "blue/red alternate 1Hz");
    VK_CHECK_EQ_STR(vk_led_describe(VK_STATE_WORK_READY), "blue=ON red=OFF");
    VK_CHECK_EQ_STR(vk_led_describe(VK_STATE_ERROR), "blue=OFF red=ON");
}

void test_led_fsm_run(void)
{
    vk_suite_name = "led_fsm";
    (void)printf("  [led_fsm] PRD 5.1 五档状态逐项 + 红蓝交替互斥\n");
    suite_led_states();
    suite_led_ap_alternate();
    suite_led_fail_blink();
    suite_led_pattern_table();
    suite_led_describe();
}
