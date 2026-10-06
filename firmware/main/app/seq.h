/**
 * @file seq.h
 * @brief 上行帧序号分配器。
 *
 * 冻结依据：docs/00-技术设计文档.md §3.4
 *
 * 规则 1：上行 SEQ 由设备侧统一分配，单调递增，16 位回绕。
 * 规则 2：当一个事件需要同时走 USB 和 WiFi 时，两条帧使用完全相同的 SEQ。
 *         因此分配器必须在"构造帧之前"调用一次，而不是每条通道各调一次。
 */
#ifndef VIBEKEY_SEQ_H
#define VIBEKEY_SEQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t next;
} vk_seq_t;

/** 初始化。首个分配出的序号为 1（0 保留表示"无序号"）。 */
void vk_seq_init(vk_seq_t *s);

/** 取一个序号并自增。 */
uint16_t vk_seq_next(vk_seq_t *s);

/** 当前下一个将被分配的序号（不断状态）。 */
uint16_t vk_seq_peek(const vk_seq_t *s);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_SEQ_H */
