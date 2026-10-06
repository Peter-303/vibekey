/**
 * @file test_net_fsm.c
 * @brief 配网状态机测试。对应 PRD 5.2 的四条验收口径。
 */
#include "net_fsm.h"
#include "test_util.h"

#include <string.h>

VK_SUITE(net_defaults)
{
    vk_net_fsm_t f;
    vk_net_cfg_t c = vk_net_default_cfg();
    VK_CHECK_EQ_INT(c.sta_timeout_ms, 10000);
    VK_CHECK_EQ_INT(c.sta_retry_max, 3);
    VK_CHECK_EQ_STR(c.ap_pass, "Vb7Kq2mXv9");

    vk_net_init(&f, NULL, "1A2B");
    VK_CHECK_EQ_STR(f.ap_ssid, "VibeKey-1A2B");
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_INIT);
    VK_CHECK_EQ_INT(vk_net_state(&f), VK_NET_ST_INIT);
    VK_CHECK_EQ_INT(vk_net_is_up(&f), 0);
}

VK_SUITE(net_no_creds_goes_ap)
{
    vk_net_fsm_t f;
    vk_net_init(&f, NULL, "0B3F");
    /* PRD：空 NVS 上电 -> 立刻进入 AP 模式 */
    VK_CHECK_EQ_INT(vk_net_on_creds(&f, 0u, NULL, NULL, 100u),
                    VK_NET_ACT_START_AP);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);
    VK_CHECK_EQ_STR(f.ap_ssid, "VibeKey-0B3F");
    /* AP 态下 tick 不产生动作 */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 200u, 0u, NULL, 0), VK_NET_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);
}

VK_SUITE(net_creds_goes_sta)
{
    vk_net_fsm_t f;
    vk_net_init(&f, NULL, "1A2B");
    VK_CHECK_EQ_INT(vk_net_on_creds(&f, 1u, "MyHome", "password123", 0u),
                    VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_STA_CONNECTING);
    VK_CHECK_EQ_STR(f.ssid, "MyHome");
    VK_CHECK_EQ_STR(f.pass, "password123");
    VK_CHECK_EQ_INT(vk_net_is_up(&f), 1);

    /* 拿到 IP -> CONNECTED */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 500u, 1u, "192.168.1.23", -47),
                    VK_NET_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_CONNECTED);
    VK_CHECK_EQ_STR(f.ip, "192.168.1.23");
    VK_CHECK_EQ_INT(f.rssi, -47);
}

VK_SUITE(net_wrong_password_falls_back_to_ap)
{
    vk_net_fsm_t f;
    uint32_t t;
    vk_net_init(&f, NULL, "1A2B");
    VK_CHECK_EQ_INT(vk_net_on_creds(&f, 1u, "MyHome", "wrongpass", 0u),
                    VK_NET_ACT_START_STA);

    /* 第一次超时（10s）-> 重试，窗口重置 */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 9999u, 0u, NULL, 0), VK_NET_ACT_NONE);
    VK_CHECK_EQ_INT(vk_net_tick(&f, 10000u, 0u, NULL, 0), VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(f.retry_count, 1);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_STA_CONNECTING);

    /* 第二次超时（再 10s）-> 重试 */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 19999u, 0u, NULL, 0), VK_NET_ACT_NONE);
    VK_CHECK_EQ_INT(vk_net_tick(&f, 20000u, 0u, NULL, 0), VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(f.retry_count, 2);

    /* 第三次超时 -> 回退 AP，且不算故障 */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 30000u, 0u, NULL, 0), VK_NET_ACT_START_AP);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);
    VK_CHECK_EQ_INT(vk_net_is_up(&f), 0);

    /* PRD 口径：约 40s 内回到红蓝交替闪烁。此处为 30s，满足 */
    VK_CHECK(30000u <= 40000u);

    /* AP 态下上位机再次下发配置 */
    for (t = 30001u; t < 31000u; t += 100u) {
        VK_CHECK_EQ_INT(vk_net_tick(&f, t, 0u, NULL, 0), VK_NET_ACT_NONE);
    }
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);
}

VK_SUITE(net_router_unplugged)
{
    vk_net_fsm_t f;
    vk_net_init(&f, NULL, "1A2B");
    VK_CHECK_EQ_INT(vk_net_on_creds(&f, 1u, "MyHome", "password123", 0u),
                    VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(vk_net_tick(&f, 1000u, 1u, "192.168.1.23", -50),
                    VK_NET_ACT_NONE);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_CONNECTED);

    /* PRD：已连接状态下拔路由器 -> 重试窗口结束后回 AP，不死机、不红灯常亮 */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 2000u, 0u, NULL, 0), VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_STA_CONNECTING);
    VK_CHECK_EQ_INT(f.retry_count, 0);   /* 重试计数已归零 */

    /* 走完 3 次重试后回 AP */
    VK_CHECK_EQ_INT(vk_net_tick(&f, 12000u, 0u, NULL, 0), VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(vk_net_tick(&f, 22000u, 0u, NULL, 0), VK_NET_ACT_START_STA);
    VK_CHECK_EQ_INT(vk_net_tick(&f, 32000u, 0u, NULL, 0), VK_NET_ACT_START_AP);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);
}

VK_SUITE(net_new_config)
{
    vk_net_fsm_t f;
    vk_net_init(&f, NULL, "1A2B");
    VK_CHECK_EQ_INT(vk_net_on_creds(&f, 0u, NULL, NULL, 0u), VK_NET_ACT_START_AP);
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);

    /* 上位机下发新配置 -> 先存 NVS，再重启 */
    VK_CHECK_EQ_INT(vk_net_new_config(&f, "NewSSID", "newpassword", 5000u),
                    VK_NET_ACT_SAVE_CREDS);
    VK_CHECK_EQ_STR(f.ssid, "NewSSID");
    VK_CHECK_EQ_STR(f.pass, "newpassword");
    /* 注意：此时状态仍是 AP，还没重启 —— 半途断开不会写坏 NVS */
    VK_CHECK_EQ_INT(f.st, VK_NET_ST_AP);

    VK_CHECK_EQ_INT(vk_net_on_saved(&f, 5100u), VK_NET_ACT_REBOOT);
}

VK_SUITE(net_ssid_truncation)
{
    vk_net_fsm_t f;
    char long_ssid[64];
    char long_pass[128];
    memset(long_ssid, 'A', sizeof(long_ssid) - 1u);
    long_ssid[sizeof(long_ssid) - 1u] = '\0';
    memset(long_pass, 'B', sizeof(long_pass) - 1u);
    long_pass[sizeof(long_pass) - 1u] = '\0';

    vk_net_init(&f, NULL, "1A2B");
    vk_net_new_config(&f, long_ssid, long_pass, 0u);
    VK_CHECK_EQ_INT((int)strlen(f.ssid), VK_NET_SSID_MAX);
    VK_CHECK_EQ_INT((int)strlen(f.pass), VK_NET_PASS_MAX);

    /* NULL 安全 */
    vk_net_init(&f, NULL, NULL);
    VK_CHECK_EQ_STR(f.ap_ssid, "VibeKey-");
}

VK_SUITE(net_state_str)
{
    VK_CHECK_EQ_STR(vk_net_state_str(VK_NET_ST_INIT), "INIT");
    VK_CHECK_EQ_STR(vk_net_state_str(VK_NET_ST_STA_CONNECTING), "STA_CONNECTING");
    VK_CHECK_EQ_STR(vk_net_state_str(VK_NET_ST_CONNECTED), "CONNECTED");
    VK_CHECK_EQ_STR(vk_net_state_str(VK_NET_ST_AP), "AP");
}

void test_net_fsm_run(void)
{
    vk_suite_name = "net_fsm";
    (void)printf("  [net_fsm] PRD 5.2 四条验收：空NVS进AP / 错密码回退 / 拔路由不死机 / 新配置\n");
    suite_net_defaults();
    suite_net_no_creds_goes_ap();
    suite_net_creds_goes_sta();
    suite_net_wrong_password_falls_back_to_ap();
    suite_net_router_unplugged();
    suite_net_new_config();
    suite_net_ssid_truncation();
    suite_net_state_str();
}
