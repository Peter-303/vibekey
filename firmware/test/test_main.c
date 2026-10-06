/**
 * @file test_main.c
 * @brief 应用层测试入口。构建与运行方式见 firmware/test/Makefile。
 *
 * 设计目的：让固件应用层在**没有任何硬件、没有 ESP-IDF** 的前提下
 * 就能被完整验证（ADR-1 的直接收益）。
 */
#include "test_util.h"

#include <stdio.h>

int vk_tests_run = 0;
int vk_tests_failed = 0;
const char *vk_suite_name = "";

void test_proto_run(void);
void test_status_run(void);
void test_key_fsm_run(void);
void test_led_fsm_run(void);
void test_seq_run(void);
void test_cmd_dispatch_run(void);
void test_net_fsm_run(void);
void test_asr_fsm_run(void);
void test_app_run(void);

int main(void)
{
    (void)printf("VibeKey 固件应用层测试\n");
    (void)printf("协议冻结依据：docs/00-技术设计文档.md\n\n");

    test_proto_run();
    test_status_run();
    test_key_fsm_run();
    test_led_fsm_run();
    test_seq_run();
    test_cmd_dispatch_run();
    test_net_fsm_run();
    test_asr_fsm_run();
    test_app_run();

    (void)printf("\n----------------------------------------\n");
    if (vk_tests_failed == 0) {
        (void)printf("通过：%d 项断言全部成功\n", vk_tests_run);
        return 0;
    }
    (void)printf("失败：%d / %d 项断言未通过\n", vk_tests_failed, vk_tests_run);
    return 1;
}
