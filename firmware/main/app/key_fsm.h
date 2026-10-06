/**
 * @file key_fsm.h
 * @brief 按键 / 旋转编码器扫描与消抖状态机。
 *
 * 冻结依据：docs/00-技术设计文档.md §4、§5.3
 *
 * 设计约束（全部来自 PRD 验收条款）：
 *   - 消抖 15ms（PRD 5.4 要求 10--20ms 内电平稳定后再判定）
 *   - 一次按下恰好产生 1 个事件，长按不连发
 *   - 编码器每个机械档位（detent）恰好 1 个方向事件，不得出现两个方向
 *
 * 本模块是纯函数式的：输入原始电平采样与时间戳，输出事件列表。
 * 不依赖任何 ESP-IDF 符号，可在主机上完整单测（见 test/test_key_fsm.c）。
 */
#ifndef VIBEKEY_KEY_FSM_H
#define VIBEKEY_KEY_FSM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_KEY_COUNT        7u   /**< 机械按键数量 */
#define VK_RAW_INPUT_COUNT  10u  /**< 7 键 + 编码器 A + 编码器 B + 编码器按下 */

/* raw[] 数组下标 */
#define VK_IDX_KEY_BASE   0u   /**< K1..K7 -> raw[0..6] */
#define VK_IDX_ENC_A      7u
#define VK_IDX_ENC_B      8u
#define VK_IDX_ENC_SW     9u

#define VK_DEBOUNCE_MS    15u  /**< 消抖时间，PRD 5.4 区间中点 */
#define VK_SAMPLE_MS       5u  /**< 采样周期 */

/* 默认按键字符表（设计文档 §4 已冻结） */
#define VK_CHAR_KEY_1     '1'
#define VK_CHAR_KEY_2     '2'
#define VK_CHAR_KEY_3     '3'
#define VK_CHAR_KEY_4     '4'
#define VK_CHAR_KEY_5     '5'
#define VK_CHAR_KEY_6     '6'
#define VK_CHAR_KEY_7     '7'
#define VK_CHAR_ENC_CW    '+'
#define VK_CHAR_ENC_CCW   '-'
#define VK_CHAR_ENC_SW    '0'

/* 事件类型 */
typedef enum {
    VK_KEY_EV_NONE = 0,
    VK_KEY_EV_PRESS,
    VK_KEY_EV_RELEASE
} vk_key_ev_kind_t;

typedef struct {
    vk_key_ev_kind_t kind;
    char             ch;      /**< 该事件对应的 ASCII 字符 */
    uint8_t          index;   /**< 来源输入下标，调试用 */
} vk_key_event_t;

/* 单个输入的去抖状态 */
typedef struct {
    uint8_t  stable;         /**< 已确认电平：0=释放 1=按下 */
    uint8_t  pending;        /**< 待确认电平 */
    uint32_t last_change_ms; /**< pending 最后一次变化的时刻 */
} vk_debounce_t;

/* 配置 */
typedef struct {
    uint16_t debounce_ms;
    char     key_chars[VK_KEY_COUNT];   /**< '1'..'7' */
    char     enc_cw_char;
    char     enc_ccw_char;
    char     enc_sw_char;
    uint8_t  release_enabled;           /**< 是否上报释放事件（默认 0） */
    char     voice_key_char;            /**< 语音键字符，0 = 无。仅此键的释放会被上报 */
} vk_key_fsm_cfg_t;

/* 状态机 */
typedef struct {
    vk_key_fsm_cfg_t cfg;
    vk_debounce_t    in[VK_RAW_INPUT_COUNT];
    uint8_t          enc_prev;    /**< 上一拍 A/B 组合：bit0=A, bit1=B */
    int16_t          enc_accum;   /**< 正交相位累积，每 +-4 产出 1 个事件 */
    uint8_t          started;     /**< 首拍只建立基准，不产出事件 */
} vk_key_fsm_t;

/** 取默认配置（字符表见上，release_enabled=0，voice_key_char='7'）。 */
vk_key_fsm_cfg_t vk_key_fsm_default_cfg(void);

/** 初始化。cfg 传 NULL 时用默认配置。 */
void vk_key_fsm_init(vk_key_fsm_t *f, const vk_key_fsm_cfg_t *cfg);

/**
 * 喂入一拍原始采样。
 *
 * @param raw     VK_RAW_INPUT_COUNT 个电平，1=按下/高电平，0=释放/低电平
 * @param now_ms  当前时间戳（毫秒，可回绕）
 * @param events  输出事件数组，容量 cap
 * @return        >=0 产出的事件数；-1 参数非法
 */
int vk_key_fsm_tick(vk_key_fsm_t *f, const uint8_t raw[VK_RAW_INPUT_COUNT], uint32_t now_ms,
                    vk_key_event_t *events, size_t cap);

/** 某下标对应的字符（调试/测试用）。 */
char vk_key_char_for_index(const vk_key_fsm_t *f, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_KEY_FSM_H */
