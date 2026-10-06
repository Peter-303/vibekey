/**
 * @file port_esp32.h
 * @brief ESP-IDF 移植层实现的公开声明。
 */
#ifndef VIBEKEY_PORT_ESP32_H
#define VIBEKEY_PORT_ESP32_H

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 移植层需要从 main.c 拿到的"回拨"。
 *
 * 用函数指针而不是直接 include app.h：移植层不该知道 vk_app_t 的定义
 * （ADR-1 分层约束）。app 实例以 void* 透明传递。
 */
typedef struct {
    /** 应用层实例的不透明指针。 */
    void *app;

    /** 收到 USB 字节时调用，由 app 层做帧解析。 */
    void (*on_usb_bytes)(void *app, const uint8_t *data, uint32_t len);

    /** 收到 WiFi（TCP）字节时调用。可为 NULL，表示只通 USB 一条通道。 */
    void (*on_wifi_bytes)(void *app, const uint8_t *data, uint32_t len);

    /** ASR 返回一条转写文本时调用。is_final=1 表示最终结果。可为 NULL。 */
    void (*on_asr_text)(void *app, const char *utf8_text, uint8_t is_final);
} port_esp32_hooks_t;

/** ESP32-S3 移植层实例。main.c 启动时注入给应用层。 */
extern const vk_port_t port_esp32;

/**
 * 初始化移植层（在 app 之前调用）。
 *
 * @param hooks 可为 NULL（调试期不接上行数据时用）。
 * @return 0 成功；负值失败（失败时 LED 会进入 ERROR 表现）
 */
int port_esp32_init(const port_esp32_hooks_t *hooks);

/** 取得本机 MAC 后 4 位十六进制大写字符串（如 "1A2B"），用于 AP SSID。 */
void port_esp32_get_mac4(char out[8]);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_PORT_ESP32_H */
