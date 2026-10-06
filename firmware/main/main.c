/**
 * @file main.c
 * @brief ESP-IDF 入口：装配移植层与应用层。
 *
 * 架构约束：本文件是唯一同时认识 app/ 与 port/ 的地方，职责只有
 *   1. 初始化 NVS 与移植层
 *   2. 把 vk_port 指向 port_esp32，并注册"上行字节 -> app"的回拨
 *   3. 初始化应用层
 *   4. 进入主循环
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app.h"
#include "port.h"
#include "port_esp32.h"

static const char *TAG = "vibekey";

/* 移植层实例注入点（app/port.h 声明，定义在此） */
const vk_port_t *vk_port = &port_esp32;

static vk_app_t s_app;

/* ---- 移植层的回拨：把各通道收到的字节转给应用层做帧解析 ----
 *
 * 注意：这里只交"字节"，不做任何帧解析。解码、SEQ 去重、分发全在 app 层
 * （vk_app_on_usb_bytes / vk_app_on_wifi_bytes 内部各自做），
 * 因为跨通道去重需要统一视图（设计文档 §3.4 规则 3）。 */
static void on_usb_bytes(void *app, const uint8_t *data, uint32_t len)
{
    vk_app_on_usb_bytes((vk_app_t *)app, data, len);
}

static void on_wifi_bytes(void *app, const uint8_t *data, uint32_t len)
{
    vk_app_on_wifi_bytes((vk_app_t *)app, data, len);
}

void app_main(void)
{
    ESP_LOGI(TAG, "VibeKey 固件启动，版本 %s", CONFIG_VIBEKEY_FW_VERSION);

    /* NVS 必须先初始化：配网凭据与 ASR 配置都依赖它 */
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* 移植层：在 app 之前起来，因为 vk_app_init 要用 vk_port */
    port_esp32_hooks_t hooks = {
        .app = &s_app,
        .on_usb_bytes = on_usb_bytes,
        .on_wifi_bytes = on_wifi_bytes,
    };
    if (port_esp32_init(&hooks) != 0) {
        ESP_LOGE(TAG, "移植层初始化失败");
        /* 移植层内部会把 LED 打到 ERROR 表现 */
        return;
    }

    vk_app_init(&s_app);

    ESP_LOGI(TAG, "进入主循环");
    for (;;) {
        (void)vk_app_poll(&s_app);
    }
}
