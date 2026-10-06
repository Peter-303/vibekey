/**
 * @file seq.c
 * @brief 序号分配器实现。
 */
#include "seq.h"
#include <stddef.h>   /* NULL */

void vk_seq_init(vk_seq_t *s)
{
    if (s == NULL) {
        return;
    }
    s->next = 1u;
}

uint16_t vk_seq_next(vk_seq_t *s)
{
    uint16_t v;
    if (s == NULL) {
        return 0u;
    }
    v = s->next;
    s->next = (uint16_t)(s->next + 1u);   /* 自然回绕 */
    return v;
}

uint16_t vk_seq_peek(const vk_seq_t *s)
{
    return (s == NULL) ? 0u : s->next;
}
