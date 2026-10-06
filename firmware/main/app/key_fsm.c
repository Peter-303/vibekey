/**
 * @file key_fsm.c
 * @brief 按键 / 编码器消抖与事件生成实现。
 *
 * 正交解码表已用 Python 预先验证：
 *   顺时针 00->01->11->10->00 累积 +4，逆时针累积 -4，
 *   单个 detent 恰好 1 个事件，半档折返与无效跳变被阈值吸收。
 */
#include "key_fsm.h"
#include <stddef.h>   /* NULL */

/* 正交解码表：行=上一拍 AB 状态，列=当前 AB 状态；AB = A | (B<<1) */
static const int8_t vk_enc_table[4][4] = {
    {  0,  1, -1,  0 },
    { -1,  0,  0,  1 },
    {  1,  0,  0, -1 },
    {  0, -1,  1,  0 }
};

#define VK_ENC_DETENT 4   /**< 一个机械档位对应的有效跳变数 */

vk_key_fsm_cfg_t vk_key_fsm_default_cfg(void)
{
    vk_key_fsm_cfg_t c;
    c.debounce_ms    = (uint16_t)VK_DEBOUNCE_MS;
    c.key_chars[0]   = (char)VK_CHAR_KEY_1;
    c.key_chars[1]   = (char)VK_CHAR_KEY_2;
    c.key_chars[2]   = (char)VK_CHAR_KEY_3;
    c.key_chars[3]   = (char)VK_CHAR_KEY_4;
    c.key_chars[4]   = (char)VK_CHAR_KEY_5;
    c.key_chars[5]   = (char)VK_CHAR_KEY_6;
    c.key_chars[6]   = (char)VK_CHAR_KEY_7;
    c.enc_cw_char    = (char)VK_CHAR_ENC_CW;
    c.enc_ccw_char   = (char)VK_CHAR_ENC_CCW;
    c.enc_sw_char    = (char)VK_CHAR_ENC_SW;
    c.release_enabled = 0u;
    c.voice_key_char = (char)VK_CHAR_KEY_7;
    return c;
}

void vk_key_fsm_init(vk_key_fsm_t *f, const vk_key_fsm_cfg_t *cfg)
{
    unsigned i;
    if (f == NULL) {
        return;
    }
    f->cfg = (cfg != NULL) ? *cfg : vk_key_fsm_default_cfg();
    if (f->cfg.debounce_ms == 0u) {
        f->cfg.debounce_ms = (uint16_t)VK_DEBOUNCE_MS;
    }
    for (i = 0u; i < VK_RAW_INPUT_COUNT; i++) {
        f->in[i].stable = 0u;
        f->in[i].pending = 0u;
        f->in[i].last_change_ms = 0u;
    }
    f->enc_prev = 0u;
    f->enc_accum = 0;
    f->started = 0u;
}

char vk_key_char_for_index(const vk_key_fsm_t *f, uint8_t index)
{
    if (f == NULL) {
        return '\0';
    }
    if (index < VK_KEY_COUNT) {
        return f->cfg.key_chars[index];
    }
    switch (index) {
    case VK_IDX_ENC_A:  return '\0';   /* A/B 相不产生字符 */
    case VK_IDX_ENC_B:  return '\0';
    case VK_IDX_ENC_SW: return f->cfg.enc_sw_char;
    default:            return '\0';
    }
}

static int emit(vk_key_event_t *events, size_t cap, size_t *n,
                vk_key_ev_kind_t kind, char ch, uint8_t index)
{
    if (*n >= cap) {
        return -1;   /* 输出已满，丢弃 */
    }
    events[*n].kind = kind;
    events[*n].ch = ch;
    events[*n].index = index;
    (*n)++;
    return 0;
}

/**
 * 对单个输入做去抖。返回 1 表示本次 tick 发生了"已确认的电平翻转"。
 */
static int debounce_update(vk_debounce_t *d, uint8_t sample, uint32_t now_ms, uint16_t db_ms)
{
    if (sample != d->pending) {
        d->pending = sample;
        d->last_change_ms = now_ms;
        return 0;
    }
    if ((d->pending != d->stable) && ((now_ms - d->last_change_ms) >= db_ms)) {
        d->stable = d->pending;
        return 1;
    }
    return 0;
}

int vk_key_fsm_tick(vk_key_fsm_t *f, const uint8_t raw[VK_RAW_INPUT_COUNT], uint32_t now_ms,
                    vk_key_event_t *events, size_t cap)
{
    size_t n = 0u;
    unsigned i;
    uint8_t ab;
    int8_t delta;

    if ((f == NULL) || (raw == NULL)) {
        return -1;
    }
    if ((events == NULL) && (cap > 0u)) {
        return -1;
    }

    /* --- 1. 编码器 A/B 相去抖（不产生按下/释放事件） --- */
    for (i = VK_IDX_ENC_A; i <= VK_IDX_ENC_B; i++) {
        (void)debounce_update(&f->in[i], raw[i], now_ms, f->cfg.debounce_ms);
    }

    ab = (uint8_t)((uint8_t)(f->in[VK_IDX_ENC_A].stable ? 1u : 0u)
                 | (uint8_t)(f->in[VK_IDX_ENC_B].stable ? 2u : 0u));

    if (f->started == 0u) {
        f->started = 1u;
        f->enc_prev = ab;
        f->enc_accum = 0;
    } else if (ab != f->enc_prev) {
        delta = vk_enc_table[f->enc_prev][ab];
        f->enc_prev = ab;
        f->enc_accum = (int16_t)(f->enc_accum + delta);
        while (f->enc_accum >= (int16_t)VK_ENC_DETENT) {
            f->enc_accum = (int16_t)(f->enc_accum - (int16_t)VK_ENC_DETENT);
            (void)emit(events, cap, &n, VK_KEY_EV_PRESS, f->cfg.enc_cw_char, VK_IDX_ENC_A);
        }
        while (f->enc_accum <= -(int16_t)VK_ENC_DETENT) {
            f->enc_accum = (int16_t)(f->enc_accum + (int16_t)VK_ENC_DETENT);
            (void)emit(events, cap, &n, VK_KEY_EV_PRESS, f->cfg.enc_ccw_char, VK_IDX_ENC_B);
        }
    }

    /* --- 2. 7 个机械键 + 编码器按下 --- */
    for (i = 0u; i < VK_RAW_INPUT_COUNT; i++) {
        char ch;
        int flipped;

        if ((i == VK_IDX_ENC_A) || (i == VK_IDX_ENC_B)) {
            continue;   /* A/B 相已在上面处理 */
        }
        flipped = debounce_update(&f->in[i], raw[i], now_ms, f->cfg.debounce_ms);
        if (flipped == 0) {
            continue;
        }
        ch = vk_key_char_for_index(f, (uint8_t)i);
        if (f->in[i].stable != 0u) {
            (void)emit(events, cap, &n, VK_KEY_EV_PRESS, ch, (uint8_t)i);
        } else if ((f->cfg.release_enabled != 0u)
                   && (ch != '\0')
                   && (ch == f->cfg.voice_key_char)) {
            /* 仅语音键上报释放事件，见设计文档 §5.4 */
            (void)emit(events, cap, &n, VK_KEY_EV_RELEASE, ch, (uint8_t)i);
        }
    }

    return (int)n;
}
