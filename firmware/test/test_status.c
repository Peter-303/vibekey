/**
 * @file test_status.c
 * @brief 状态上报字符串测试。格式冻结依据 docs/00-技术设计文档.md §3.5。
 */
#include "status.h"
#include "test_util.h"

#include <string.h>

VK_SUITE(status_str_tables)
{
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_BOOT), "BOOT");
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_AP_START), "AP_START");
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_STA_JOIN_OK), "STA_JOIN_OK");
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_STA_JOIN_FAIL), "STA_JOIN_FAIL");
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_WORK_READY), "WORK_READY");
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_ERROR), "ERROR");
    VK_CHECK_EQ_STR(vk_state_str(VK_STATE_DOWNLOAD), "DOWNLOAD");
    VK_CHECK_EQ_STR(vk_state_str((vk_state_t)0xEE), "UNKNOWN");

    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_NONE), "NONE");
    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_WIFI_FAIL), "WIFI_FAIL");
    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_MIC_FAIL), "MIC_FAIL");
    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_ASR_INIT_FAIL), "ASR_INIT_FAIL");
    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_USB_FAIL), "USB_FAIL");
    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_NVS_FAIL), "NVS_FAIL");
    VK_CHECK_EQ_STR(vk_err_str(VK_ERR_WDT_RESET), "WDT_RESET");

    VK_CHECK_EQ_STR(vk_warn_str(VK_WARN_NONE), "NONE");
    VK_CHECK_EQ_STR(vk_warn_str(VK_WARN_ASR_UNAVAILABLE), "ASR_UNAVAILABLE");
    VK_CHECK_EQ_STR(vk_warn_str(VK_WARN_RSSI_LOW), "RSSI_LOW");
    VK_CHECK_EQ_STR(vk_warn_str(VK_WARN_MIC_DEGRADED), "MIC_DEGRADED");

    VK_CHECK_EQ_STR(vk_asr_str(VK_ASR_OK), "OK");
    VK_CHECK_EQ_STR(vk_asr_str(VK_ASR_BUSY), "BUSY");
    VK_CHECK_EQ_STR(vk_asr_str(VK_ASR_DOWN), "DOWN");
    VK_CHECK_EQ_STR(vk_asr_str(VK_ASR_UNKNOWN), "");
}

VK_SUITE(status_init)
{
    vk_status_t st;
    vk_status_init(&st);
    VK_CHECK_EQ_INT(st.state, VK_STATE_BOOT);
    VK_CHECK_EQ_INT(st.err, VK_ERR_NONE);
    VK_CHECK_EQ_INT(st.warn, VK_WARN_NONE);
    VK_CHECK_EQ_INT(st.asr, VK_ASR_UNKNOWN);
    VK_CHECK_EQ_INT(st.rssi, 0);
    VK_CHECK_EQ_STR(st.fw, "");
    VK_CHECK_EQ_STR(st.ip, "");
    VK_CHECK_EQ_STR(st.ssid, "");
}

VK_SUITE(status_format)
{
    vk_status_t st;
    char buf[128];
    int n;

    /* 正常工作态：与设计文档 §3.3 示例一致 */
    vk_status_init(&st);
    st.state = VK_STATE_WORK_READY;
    strcpy(st.fw, "1.0.0");
    st.rssi = -58;
    st.asr = VK_ASR_OK;
    n = vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_INT(n, 35);
    VK_CHECK_EQ_STR(buf, "WORK_READY,FW=1.0.0,RSSI=-58,ASR=OK");

    /* AP 态：带自身热点名 */
    vk_status_init(&st);
    st.state = VK_STATE_AP_START;
    strcpy(st.fw, "1.0.0");
    strcpy(st.ssid, "VibeKey-1A2B");
    vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "AP_START,FW=1.0.0,SSID=VibeKey-1A2B");

    /* STA 成功：带 IP */
    vk_status_init(&st);
    st.state = VK_STATE_STA_JOIN_OK;
    strcpy(st.fw, "1.0.0");
    strcpy(st.ip, "192.168.1.23");
    st.rssi = -47;
    vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "STA_JOIN_OK,FW=1.0.0,IP=192.168.1.23,RSSI=-47");

    /* 故障态：带故障码 */
    vk_status_init(&st);
    st.state = VK_STATE_ERROR;
    strcpy(st.fw, "1.0.0");
    st.err = VK_ERR_MIC_FAIL;
    vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "ERROR,FW=1.0.0,ERR=MIC_FAIL");

    /* 关键场景：ASR 不可用 -> 状态仍是 WORK_READY，只多一个 WARN 字段，LED 不变 */
    vk_status_init(&st);
    st.state = VK_STATE_WORK_READY;
    strcpy(st.fw, "1.0.0");
    st.warn = VK_WARN_ASR_UNAVAILABLE;
    vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "WORK_READY,FW=1.0.0,WARN=ASR_UNAVAILABLE");

    /* 全部字段 */
    vk_status_init(&st);
    st.state = VK_STATE_WORK_READY;
    strcpy(st.fw, "1.2.3");
    strcpy(st.ip, "10.0.0.5");
    strcpy(st.ssid, "MyRouter");
    st.rssi = -70;
    st.err = VK_ERR_WIFI_FAIL;
    st.warn = VK_WARN_RSSI_LOW;
    st.asr = VK_ASR_BUSY;
    vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf,
        "WORK_READY,FW=1.2.3,SSID=MyRouter,IP=10.0.0.5,RSSI=-70,"
        "ERR=WIFI_FAIL,WARN=RSSI_LOW,ASR=BUSY");

    /* 最小快照：只有状态码 */
    vk_status_init(&st);
    st.state = VK_STATE_BOOT;
    vk_status_format(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "BOOT");
}

VK_SUITE(status_format_uart1)
{
    vk_status_t st;
    char buf[128];

    /* 与 PRD 5.3 的示例帧格式一致：STATUS:<...>\r\n */
    vk_status_init(&st);
    st.state = VK_STATE_WORK_READY;
    strcpy(st.fw, "1.0.0");
    vk_status_format_uart1(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "STATUS:WORK_READY,FW=1.0.0\r\n");

    vk_status_init(&st);
    st.state = VK_STATE_AP_START;
    strcpy(st.fw, "1.0.0");
    strcpy(st.ssid, "VibeKey-1234");
    vk_status_format_uart1(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "STATUS:AP_START,FW=1.0.0,SSID=VibeKey-1234\r\n");

    vk_status_init(&st);
    st.state = VK_STATE_ERROR;
    strcpy(st.fw, "1.0.0");
    st.err = VK_ERR_ASR_INIT_FAIL;
    vk_status_format_uart1(&st, buf, sizeof(buf));
    VK_CHECK_EQ_STR(buf, "STATUS:ERROR,FW=1.0.0,ERR=ASR_INIT_FAIL\r\n");
}

VK_SUITE(status_truncation)
{
    vk_status_t st;
    char tiny[8];

    /* 缓冲不足时必须 NUL 结尾且不越界 */
    vk_status_init(&st);
    st.state = VK_STATE_WORK_READY;
    strcpy(st.fw, "1.0.0");
    st.rssi = -58;
    vk_status_format(&st, tiny, sizeof(tiny));
    VK_CHECK_EQ_INT(tiny[7], '\0');
    VK_CHECK(strlen(tiny) < sizeof(tiny));

    /* 空参数 */
    VK_CHECK_EQ_INT(vk_status_format(NULL, tiny, sizeof(tiny)), -1);
    VK_CHECK_EQ_INT(vk_status_format(&st, NULL, 8), -1);
    VK_CHECK_EQ_INT(vk_status_format(&st, tiny, 0), -1);
}

void test_status_run(void)
{
    vk_suite_name = "status";
    (void)printf("  [status] 状态码表 / 上报字符串 / UART1 行 / 截断安全\n");
    suite_status_str_tables();
    suite_status_init();
    suite_status_format();
    suite_status_format_uart1();
    suite_status_truncation();
}
