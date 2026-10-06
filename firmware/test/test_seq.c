/**
 * @file test_seq.c
 * @brief 序号分配器测试。冻结依据 docs/00-技术设计文档.md §3.4。
 */
#include "seq.h"
#include "test_util.h"

VK_SUITE(seq_basic)
{
    vk_seq_t s;
    vk_seq_init(&s);
    VK_CHECK_EQ_INT(vk_seq_peek(&s), 1);   /* 0 保留表示"无序号" */
    VK_CHECK_EQ_INT(vk_seq_next(&s), 1);
    VK_CHECK_EQ_INT(vk_seq_next(&s), 2);
    VK_CHECK_EQ_INT(vk_seq_next(&s), 3);
    VK_CHECK_EQ_INT(vk_seq_peek(&s), 4);
}

VK_SUITE(seq_wrap)
{
    vk_seq_t s;
    int i;
    vk_seq_init(&s);
    s.next = 65535u;
    VK_CHECK_EQ_INT(vk_seq_next(&s), 65535);
    VK_CHECK_EQ_INT(vk_seq_next(&s), 0);      /* 回绕 */
    VK_CHECK_EQ_INT(vk_seq_next(&s), 1);
    /* 连续 70000 次不得重复（16 位空间 65536，此处验证单调不回退到已用值） */
    vk_seq_init(&s);
    {
        uint16_t prev = vk_seq_next(&s);
        for (i = 0; i < 70000; i++) {
            uint16_t cur = vk_seq_next(&s);
            VK_CHECK_EQ_INT(cur, (int)(uint16_t)(prev + 1u));
            prev = cur;
        }
    }
}

VK_SUITE(seq_null)
{
    vk_seq_t s;
    vk_seq_init(&s);
    vk_seq_init(NULL);              /* 不得崩溃 */
    VK_CHECK_EQ_INT(vk_seq_next(NULL), 0);
    VK_CHECK_EQ_INT(vk_seq_peek(NULL), 0);
    VK_CHECK_EQ_INT(vk_seq_next(&s), 1);   /* 状态未被破坏 */
}

void test_seq_run(void)
{
    vk_suite_name = "seq";
    (void)printf("  [seq] 单调递增 / 16 位回绕 / 空指针安全\n");
    suite_seq_basic();
    suite_seq_wrap();
    suite_seq_null();
}
