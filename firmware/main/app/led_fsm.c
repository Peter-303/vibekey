/**
 * @file led_fsm.c
 * @brief LED 状态机实现。
 *
 * 与 PRD 5.1 的 LED 状态表逐档对应：
 *   上电初始化  蓝常亮        红常亮
 *   配网中 AP   红蓝交替闪烁（同频率、互斥亮灭）
 *   STA 已连    蓝常亮        红熄灭
 *   STA 失败    蓝熄灭        红慢闪（瞬态）
 *   正常工作    蓝常亮        红熄灭
 *   故障        蓝熄灭        红常亮
 *   烧录态      由 bootloader 决定，固件不定义 -> 本模块输出全灭
 */
#include "led_fsm.h"

vk_led_pattern_t vk_led_pattern_for(vk_state_t state)
{
    vk_led_pattern_t p;
    p.blue = VK_LED_MODE_OFF;
    p.red  = VK_LED_MODE_OFF;

    switch (state) {
    case VK_STATE_BOOT:
        p.blue = VK_LED_MODE_ON;
        p.red  = VK_LED_MODE_ON;
        break;
    case VK_STATE_AP_START:
        p.blue = VK_LED_MODE_BLINK;
        p.red  = VK_LED_MODE_BLINK;
        break;
    case VK_STATE_STA_JOIN_OK:
    case VK_STATE_WORK_READY:
        p.blue = VK_LED_MODE_ON;
        break;
    case VK_STATE_STA_JOIN_FAIL:
        p.red = VK_LED_MODE_BLINK;
        break;
    case VK_STATE_ERROR:
        p.red = VK_LED_MODE_ON;
        break;
    case VK_STATE_DOWNLOAD:
    default:
        /* 下载模式由 bootloader 决定，固件不定义；输出全灭最安全 */
        break;
    }
    return p;
}

vk_led_output_t vk_led_output(vk_state_t state, uint32_t now_ms)
{
    vk_led_output_t o;
    uint32_t phase = now_ms % (uint32_t)VK_LED_BLINK_PERIOD_MS;

    o.blue_on = 0u;
    o.red_on  = 0u;

    switch (state) {
    case VK_STATE_BOOT:
        o.blue_on = 1u;
        o.red_on  = 1u;
        break;
    case VK_STATE_AP_START:
        /* 红蓝交替：前 500ms 蓝亮，后 500ms 红亮，同频率且互斥 */
        o.blue_on = (phase < (uint32_t)VK_LED_BLINK_HALF_MS) ? 1u : 0u;
        o.red_on  = (phase < (uint32_t)VK_LED_BLINK_HALF_MS) ? 0u : 1u;
        break;
    case VK_STATE_STA_JOIN_OK:
    case VK_STATE_WORK_READY:
        o.blue_on = 1u;
        break;
    case VK_STATE_STA_JOIN_FAIL:
        o.red_on = (phase < (uint32_t)VK_LED_BLINK_HALF_MS) ? 1u : 0u;
        break;
    case VK_STATE_ERROR:
        o.red_on = 1u;
        break;
    case VK_STATE_DOWNLOAD:
    default:
        break;
    }
    return o;
}

const char *vk_led_describe(vk_state_t state)
{
    switch (state) {
    case VK_STATE_BOOT:          return "blue=ON red=ON";
    case VK_STATE_AP_START:      return "blue/red alternate 1Hz";
    case VK_STATE_STA_JOIN_OK:   return "blue=ON red=OFF";
    case VK_STATE_STA_JOIN_FAIL: return "blue=OFF red=slow-blink";
    case VK_STATE_WORK_READY:    return "blue=ON red=OFF";
    case VK_STATE_ERROR:         return "blue=OFF red=ON";
    case VK_STATE_DOWNLOAD:      return "bootloader-owned";
    default:                     return "off";
    }
}
