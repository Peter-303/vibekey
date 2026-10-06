/**
 * @file led_fsm.h
 * @brief LED 状态机：状态码 -> 蓝/红灯表现。
 *
 * 冻结依据：docs/00-技术设计文档.md §5.1
 *
 * PRD 5.1 验收要求："任意状态切换后 500ms 内 LED 表现与表中一致"。
 * 本模块输出是 (state, now_ms) 的纯函数，状态一切换下一拍即反映，天然满足。
 */
#ifndef VIBEKEY_LED_FSM_H
#define VIBEKEY_LED_FSM_H

#include <stdint.h>
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 闪烁周期（毫秒）。AP 态红蓝交替、STA_JOIN_FAIL 红灯慢闪共用。 */
#define VK_LED_BLINK_PERIOD_MS 1000u
/** 亮暗分界：周期内前 500ms 与后 500ms 分别点亮不同灯，实现"互斥亮灭"。 */
#define VK_LED_BLINK_HALF_MS   500u

typedef enum {
    VK_LED_MODE_OFF = 0,
    VK_LED_MODE_ON,
    VK_LED_MODE_BLINK
} vk_led_mode_t;

typedef struct {
    vk_led_mode_t blue;
    vk_led_mode_t red;
} vk_led_pattern_t;

typedef struct {
    uint8_t blue_on;
    uint8_t red_on;
} vk_led_output_t;

/** 查表：某状态对应的灯模式（供移植层做定时器配置）。 */
vk_led_pattern_t vk_led_pattern_for(vk_state_t state);

/** 某状态下、给定时刻的灯电平。纯函数，可直接用于测试。 */
vk_led_output_t vk_led_output(vk_state_t state, uint32_t now_ms);

/** 人类可读描述，供 UART1 调试与测试断言。 */
const char *vk_led_describe(vk_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_LED_FSM_H */
