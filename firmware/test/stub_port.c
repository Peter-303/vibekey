/**
 * @file stub_port.c
 * @brief 主机端测试用的假移植层实现。
 *
 * 提供 `vk_port` 符号（真实实现由 port/port_esp32.c 在上板时提供），
 * 并记录调用情况，便于后续为 app.c / cmd_dispatch.c 编写不依赖硬件的测试。
 */
#include "port.h"
#include "test_util.h"

#include <string.h>

/* ------------------------- 调用记录 ------------------------- */
uint8_t  vk_stub_led_blue = 0u;
uint8_t  vk_stub_led_red = 0u;
unsigned vk_stub_led_calls = 0u;
unsigned vk_stub_uart1_lines = 0u;
char     vk_stub_last_uart1[128];
unsigned vk_stub_reboots = 0u;
unsigned vk_stub_wdt_feeds = 0u;
unsigned vk_stub_frames_sent = 0u;
uint8_t  vk_stub_last_type = 0u;
uint16_t vk_stub_last_seq = 0u;
uint16_t vk_stub_last_len = 0u;
uint8_t  vk_stub_last_payload[PROTO_MAX_PAYLOAD];
uint8_t  vk_stub_last_channel = 0u;

/* 测试可控制的输入 */
uint8_t  vk_stub_raw[VK_RAW_INPUT_COUNT];
char     vk_stub_sta_ip[16];
int      vk_stub_rssi = 0;

static uint32_t stub_now_ms = 0u;

void vk_stub_reset(void)
{
    vk_stub_led_blue = 0u;
    vk_stub_led_red = 0u;
    vk_stub_led_calls = 0u;
    vk_stub_uart1_lines = 0u;
    vk_stub_last_uart1[0] = '\0';
    vk_stub_reboots = 0u;
    vk_stub_wdt_feeds = 0u;
    vk_stub_frames_sent = 0u;
    vk_stub_last_type = 0u;
    vk_stub_last_seq = 0u;
    vk_stub_last_len = 0u;
    memset(vk_stub_last_payload, 0, sizeof(vk_stub_last_payload));
    vk_stub_last_channel = 0u;
    memset(vk_stub_raw, 0, sizeof(vk_stub_raw));
    vk_stub_sta_ip[0] = '\0';
    vk_stub_rssi = 0;
    stub_now_ms = 0u;
}

void vk_stub_advance_ms(uint32_t delta)
{
    stub_now_ms += delta;
}

/* ------------------------- 假实现 ------------------------- */
static uint32_t p_now_ms(void) { return stub_now_ms; }

static void p_keys_sample(uint8_t raw[VK_RAW_INPUT_COUNT])
{
    memcpy(raw, vk_stub_raw, VK_RAW_INPUT_COUNT);
}

static void p_led_set(uint8_t blue_on, uint8_t red_on)
{
    vk_stub_led_blue = blue_on;
    vk_stub_led_red = red_on;
    vk_stub_led_calls++;
}

static void p_send_frame(const uint8_t *encoded, size_t len)
{
    /* 解析出 TYPE/SEQ/PAYLOAD 以便断言（复用 proto 解码器） */
    proto_frame_t f;
    size_t consumed = 0u;
    vk_stub_frames_sent++;
    if (len < PROTO_MIN_FRAME_LEN) {
        return;
    }
    if (proto_decode(encoded, len, &f, &consumed) != PROTO_DEC_OK) {
        return;
    }
    vk_stub_last_type = f.type;
    vk_stub_last_seq = f.seq;
    vk_stub_last_len = f.len;
    if (f.len > 0u) {
        memcpy(vk_stub_last_payload, f.payload, f.len);
    }
}

static void p_uart1_print(const char *line)
{
    vk_stub_uart1_lines++;
    if (line != NULL) {
        strncpy(vk_stub_last_uart1, line, sizeof(vk_stub_last_uart1) - 1u);
        vk_stub_last_uart1[sizeof(vk_stub_last_uart1) - 1u] = '\0';
    }
}

static void p_reboot(void) { vk_stub_reboots++; }
static void p_wdt_feed(void) { vk_stub_wdt_feeds++; }
static void p_sleep_ms(uint32_t ms) { (void)ms; }

static int p_nvs_set_str(const char *k, const char *v)
{ (void)k; (void)v; return VK_PORT_OK; }
static int p_nvs_get_str(const char *k, char *o, size_t c)
{ (void)k; (void)o; (void)c; return VK_PORT_NOENT; }
static int p_nvs_commit(void) { return VK_PORT_OK; }
static int p_nvs_erase_all(void) { return VK_PORT_OK; }

static int p_wifi_start_ap(const char *s, const char *p)
{ (void)s; (void)p; return VK_PORT_OK; }
static int p_wifi_start_sta(const char *s, const char *p)
{ (void)s; (void)p; return VK_PORT_OK; }
static void p_wifi_stop(void) { }
static int p_wifi_rssi(void) { return vk_stub_rssi; }
static int p_wifi_ip(char *o, size_t c)
{
    if ((o == NULL) || (c == 0u)) { return VK_PORT_FAIL; }
    strncpy(o, vk_stub_sta_ip, c - 1u);
    o[c - 1u] = '\0';
    return VK_PORT_OK;
}

static int p_mic_init(void) { return VK_PORT_OK; }
static int p_asr_start(uint8_t mode) { (void)mode; return VK_PORT_OK; }
static void p_asr_stop(void) { }

static const vk_port_t stub_table = {
    p_now_ms,
    p_keys_sample,
    p_led_set,
    p_send_frame,
    p_uart1_print,
    p_reboot,
    p_wdt_feed,
    p_sleep_ms,
    p_nvs_set_str,
    p_nvs_get_str,
    p_nvs_commit,
    p_nvs_erase_all,
    p_wifi_start_ap,
    p_wifi_start_sta,
    p_wifi_stop,
    p_wifi_rssi,
    p_wifi_ip,
    p_mic_init,
    p_asr_start,
    p_asr_stop
};

const vk_port_t *vk_port = &stub_table;
