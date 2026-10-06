/**
 * @file test_key_fsm.c
 * @brief 按键 / 编码器消抖测试。全部对应 PRD 5.4 的验收条款。
 */
#include "key_fsm.h"
#include "test_util.h"

#include <string.h>

static void raw_all(uint8_t raw[VK_RAW_INPUT_COUNT], uint8_t v)
{
    memset(raw, (int)v, VK_RAW_INPUT_COUNT);
}

/** 喂入固定电平若干拍，累计产出的事件数。 */
static int feed(vk_key_fsm_t *f, const uint8_t raw[VK_RAW_INPUT_COUNT],
                uint32_t *t, int ticks, vk_key_event_t *ev, size_t cap)
{
    int total = 0;
    int i;
    for (i = 0; i < ticks; i++) {
        int n = vk_key_fsm_tick(f, raw, *t, ev + total, cap - (size_t)total);
        *t += VK_SAMPLE_MS;
        if (n > 0) {
            total += n;
        }
    }
    return total;
}

/* ---------------- 默认配置与字符表 ---------------- */
VK_SUITE(key_cfg)
{
    vk_key_fsm_cfg_t c = vk_key_fsm_default_cfg();
    VK_CHECK_EQ_INT(c.debounce_ms, VK_DEBOUNCE_MS);
    VK_CHECK_EQ_INT(c.key_chars[0], '1');
    VK_CHECK_EQ_INT(c.key_chars[6], '7');
    VK_CHECK_EQ_INT(c.enc_cw_char, '+');
    VK_CHECK_EQ_INT(c.enc_ccw_char, '-');
    VK_CHECK_EQ_INT(c.enc_sw_char, '0');
    VK_CHECK_EQ_INT(c.release_enabled, 0);
    VK_CHECK_EQ_INT(c.voice_key_char, '7');
    VK_CHECK_EQ_INT(VK_KEY_COUNT, 7);
    VK_CHECK_EQ_INT(VK_RAW_INPUT_COUNT, 10);
}

/* ---------------- PRD：一次按下恰好一个字符 ---------------- */
VK_SUITE(key_single_press)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[8];
    uint8_t raw[VK_RAW_INPUT_COUNT];
    uint32_t t = 0u;
    int n;

    vk_key_fsm_init(&f, NULL);
    raw_all(raw, 0u);
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 10, ev, 8), 0);   /* 基线，无事件 */

    /* 按下 K1，并在 15ms 内持续抖动（不应产生事件） */
    t = 1000u;
    raw[0] = 1u;
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 2, ev, 8), 0);    /* 10ms，未达阈值 */
    raw[0] = 0u;
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 1, ev, 8), 0);
    raw[0] = 1u;
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 1, ev, 8), 0);

    /* 抖动结束后持续稳定按住：应在第 15ms 那一刻恰好产出 1 个 PRESS '1' */
    n = feed(&f, raw, &t, 10, ev, 8);
    VK_CHECK_EQ_INT(n, 1);
    VK_CHECK_EQ_INT(ev[0].kind, VK_KEY_EV_PRESS);
    VK_CHECK_EQ_INT(ev[0].ch, '1');
    VK_CHECK_EQ_INT(ev[0].index, 0);

    /* 继续按住 200 拍（1 秒）：不得连发 */
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 200, ev, 8), 0);
}

/* ---------------- PRD：抖动期间不发字符 ---------------- */
VK_SUITE(key_bounce_suppressed)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[8];
    uint8_t raw[VK_RAW_INPUT_COUNT];
    uint32_t t = 0u;
    int i;
    int total = 0;

    vk_key_fsm_init(&f, NULL);
    raw_all(raw, 0u);
    total += feed(&f, raw, &t, 10, ev, 8);

    /* 20ms 内来回抖动 8 次，每次都短于 15ms 消抖窗 */
    t = 500u;
    for (i = 0; i < 8; i++) {
        raw[2] = (uint8_t)(i % 2);
        total += feed(&f, raw, &t, 2, ev + total, 8 - (size_t)total);
    }
    VK_CHECK_EQ_INT(total, 0);

    /* 抖动停止并稳定在高电平 -> 恰好 1 个事件 */
    raw[2] = 1u;
    total += feed(&f, raw, &t, 10, ev + total, 8 - (size_t)total);
    VK_CHECK_EQ_INT(total, 1);
    VK_CHECK_EQ_INT(ev[0].ch, '3');
}

/* ---------------- 释放事件：默认关闭，仅语音键上报 ---------------- */
VK_SUITE(key_release)
{
    vk_key_fsm_t f;
    vk_key_fsm_cfg_t cfg;
    vk_key_event_t ev[8];
    uint8_t raw[VK_RAW_INPUT_COUNT];
    uint32_t t = 0u;
    int n;

    /* 默认配置：不上报释放 */
    vk_key_fsm_init(&f, NULL);
    raw_all(raw, 0u);
    (void)feed(&f, raw, &t, 10, ev, 8);
    t = 100u;
    raw[6] = 1u;                       /* K7 = 语音键 */
    (void)feed(&f, raw, &t, 10, ev, 8);
    raw[6] = 0u;
    n = feed(&f, raw, &t, 10, ev, 8);
    VK_CHECK_EQ_INT(n, 0);

    /* 开启释放上报：K7 松手应上报 1 个 RELEASE '7' */
    cfg = vk_key_fsm_default_cfg();
    cfg.release_enabled = 1u;
    cfg.voice_key_char = '7';
    vk_key_fsm_init(&f, &cfg);
    raw_all(raw, 0u);
    t = 0u;
    (void)feed(&f, raw, &t, 10, ev, 8);
    raw[6] = 1u;
    (void)feed(&f, raw, &t, 10, ev, 8);
    raw[6] = 0u;
    n = feed(&f, raw, &t, 10, ev, 8);
    VK_CHECK_EQ_INT(n, 1);
    VK_CHECK_EQ_INT(ev[0].kind, VK_KEY_EV_RELEASE);
    VK_CHECK_EQ_INT(ev[0].ch, '7');

    /* 开启释放上报，但按的是 K1（非语音键）：仍不上报 */
    vk_key_fsm_init(&f, &cfg);
    raw_all(raw, 0u);
    t = 0u;
    (void)feed(&f, raw, &t, 10, ev, 8);
    raw[0] = 1u;
    (void)feed(&f, raw, &t, 10, ev, 8);
    raw[0] = 0u;
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 10, ev, 8), 0);
}

/* ---------------- 编码器：每个 detent 恰好一个方向事件 ---------------- */
/** 让编码器稳定在某个 AB 状态若干拍（>15ms 以过去抖） */
static int enc_state(vk_key_fsm_t *f, uint8_t a, uint8_t b, uint32_t *t,
                     vk_key_event_t *ev, size_t cap)
{
    uint8_t raw[VK_RAW_INPUT_COUNT];
    raw_all(raw, 0u);
    raw[VK_IDX_ENC_A] = a;
    raw[VK_IDX_ENC_B] = b;
    return feed(f, raw, t, 4, ev, cap);
}

VK_SUITE(key_encoder)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[8];
    uint32_t t = 0u;
    int n;

    vk_key_fsm_init(&f, NULL);

    /* 首拍建立基准（AB=00），不产出事件 */
    VK_CHECK_EQ_INT(enc_state(&f, 0u, 0u, &t, ev, 8), 0);

    /* 顺时针一个完整周期：00 -> 10 -> 11 -> 01 -> 00（A 相领先 B 相）*/
    n  = enc_state(&f, 1u, 0u, &t, ev, 8);
    n += enc_state(&f, 1u, 1u, &t, ev, 8);
    n += enc_state(&f, 0u, 1u, &t, ev, 8);
    n += enc_state(&f, 0u, 0u, &t, ev, 8);
    VK_CHECK_EQ_INT(n, 1);
    VK_CHECK_EQ_INT(ev[0].kind, VK_KEY_EV_PRESS);
    VK_CHECK_EQ_INT(ev[0].ch, '+');

    /* 再转两周 */
    n  = enc_state(&f, 1u, 0u, &t, ev, 8);
    n += enc_state(&f, 1u, 1u, &t, ev, 8);
    n += enc_state(&f, 0u, 1u, &t, ev, 8);
    n += enc_state(&f, 0u, 0u, &t, ev, 8);
    n += enc_state(&f, 1u, 0u, &t, ev, 8);
    n += enc_state(&f, 1u, 1u, &t, ev, 8);
    n += enc_state(&f, 0u, 1u, &t, ev, 8);
    n += enc_state(&f, 0u, 0u, &t, ev, 8);
    VK_CHECK_EQ_INT(n, 2);
}

VK_SUITE(key_encoder_ccw)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[8];
    uint32_t t = 0u;
    int n;

    vk_key_fsm_init(&f, NULL);
    VK_CHECK_EQ_INT(enc_state(&f, 0u, 0u, &t, ev, 8), 0);

    /* 逆时针一个完整周期：00 -> 01 -> 11 -> 10 -> 00（B 相领先 A 相）*/
    n  = enc_state(&f, 0u, 1u, &t, ev, 8);
    n += enc_state(&f, 1u, 1u, &t, ev, 8);
    n += enc_state(&f, 1u, 0u, &t, ev, 8);
    n += enc_state(&f, 0u, 0u, &t, ev, 8);
    VK_CHECK_EQ_INT(n, 1);
    VK_CHECK_EQ_INT(ev[0].ch, '-');
}

VK_SUITE(key_encoder_half_detent)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[8];
    uint32_t t = 0u;

    vk_key_fsm_init(&f, NULL);
    VK_CHECK_EQ_INT(enc_state(&f, 0u, 0u, &t, ev, 8), 0);

    /* 只走两拍（半个档位）-> 不得 emit */
    VK_CHECK_EQ_INT(enc_state(&f, 1u, 0u, &t, ev, 8), 0);
    VK_CHECK_EQ_INT(enc_state(&f, 1u, 1u, &t, ev, 8), 0);

    /* 折返回起点 -> 累积归零，仍不得 emit */
    VK_CHECK_EQ_INT(enc_state(&f, 1u, 0u, &t, ev, 8), 0);
    VK_CHECK_EQ_INT(enc_state(&f, 0u, 0u, &t, ev, 8), 0);
    VK_CHECK_EQ_INT(f.enc_accum, 0);
}

VK_SUITE(key_encoder_sw)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[8];
    uint8_t raw[VK_RAW_INPUT_COUNT];
    uint32_t t = 0u;
    int n;

    vk_key_fsm_init(&f, NULL);
    raw_all(raw, 0u);
    (void)feed(&f, raw, &t, 10, ev, 8);

    /* 编码器按下 = 普通按键，字符 '0' */
    t = 200u;
    raw[VK_IDX_ENC_SW] = 1u;
    n = feed(&f, raw, &t, 10, ev, 8);
    VK_CHECK_EQ_INT(n, 1);
    VK_CHECK_EQ_INT(ev[0].kind, VK_KEY_EV_PRESS);
    VK_CHECK_EQ_INT(ev[0].ch, '0');
    VK_CHECK_EQ_INT(ev[0].index, VK_IDX_ENC_SW);
}

/* ---------------- 参数与自定义字符表 ---------------- */
VK_SUITE(key_custom_cfg)
{
    vk_key_fsm_t f;
    vk_key_fsm_cfg_t cfg;
    vk_key_event_t ev[8];
    uint8_t raw[VK_RAW_INPUT_COUNT];
    uint32_t t = 0u;

    cfg = vk_key_fsm_default_cfg();
    cfg.debounce_ms = 20u;
    cfg.key_chars[0] = 'A';
    vk_key_fsm_init(&f, &cfg);

    raw_all(raw, 0u);
    (void)feed(&f, raw, &t, 10, ev, 8);
    t = 50u;
    raw[0] = 1u;
    /* 15ms 时（默认阈值已过，但本例阈值是 20ms）仍不应 emit */
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 3, ev, 8), 0);   /* 15ms */
    VK_CHECK_EQ_INT(feed(&f, raw, &t, 2, ev, 8), 1);   /* 25ms 过阈值 */
    VK_CHECK_EQ_INT(ev[0].ch, 'A');
}

/* ---------------- 非法参数 ---------------- */
VK_SUITE(key_bad_args)
{
    vk_key_fsm_t f;
    vk_key_event_t ev[4];
    uint8_t raw[VK_RAW_INPUT_COUNT];

    vk_key_fsm_init(&f, NULL);
    raw_all(raw, 0u);
    VK_CHECK_EQ_INT(vk_key_fsm_tick(NULL, raw, 0u, ev, 4), -1);
    VK_CHECK_EQ_INT(vk_key_fsm_tick(&f, NULL, 0u, ev, 4), -1);
    VK_CHECK_EQ_INT(vk_key_fsm_tick(&f, raw, 0u, NULL, 4), -1);
    VK_CHECK_EQ_INT(vk_key_fsm_tick(&f, raw, 0u, ev, 0), 0);   /* cap=0 合法，只是丢弃 */
    VK_CHECK_EQ_INT(vk_key_char_for_index(NULL, 0), 0);
}

void test_key_fsm_run(void)
{
    vk_suite_name = "key_fsm";
    (void)printf("  [key_fsm] 消抖阈值 / 一次一事件 / 不连发 / 编码器方向与档位\n");
    suite_key_cfg();
    suite_key_single_press();
    suite_key_bounce_suppressed();
    suite_key_release();
    suite_key_encoder();
    suite_key_encoder_ccw();
    suite_key_encoder_half_detent();
    suite_key_encoder_sw();
    suite_key_custom_cfg();
    suite_key_bad_args();
}
