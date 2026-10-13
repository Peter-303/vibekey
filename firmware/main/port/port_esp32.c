/**
 * @file port_esp32.c
 * @brief ESP32-S3 移植层实现。
 *
 * 硬件接线已确认为设计文档 §7.1 情形 A：USB-C 的 D+/D- 接到 GPIO20/GPIO19，
 * 板上没有独立 USB-TTL 桥。因此数据通道只有 TinyUSB CDC 一条，
 * 下载模式由 ROM 的 USB Serial/JTAG 接管同一对引脚（表现为 COM 口跳变）。
 *
 * TinyUSB 说明：ESP-IDF v5.5 起 TinyUSB 不再作为内置组件，而是从组件注册表
 * 引入（见 main/idf_component.yml），且**只提供裸 API**（tud_cdc_n_*、
 * tusb_rhport_init、tud_descriptor_*_cb），原先的 tinyusb_cdcacm_init() 封装
 * 已随内置组件一起移除。所以这里直接调裸 API 并自己写描述符回调。
 */
#include "port_esp32.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2s_std.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_websocket_client.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

/* TinyUSB：裸 API，见本文件头部说明 */
#include "tusb.h"
#include "device/usbd.h"
#include "class/cdc/cdc_device.h"

#include "port_board.h"

static const char *TAG = "port";

/* ============================ 上下挂钩子 ============================ */
static port_esp32_hooks_t s_hooks;

/* ============================ 时间 ============================ */
static uint32_t p_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void p_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ============================ 看门狗 ========================== */
static void p_wdt_feed(void)
{
    /* TODO(port): 接入 esp_task_wdt。使能 TWDT（10s 超时）后此处喂狗。
     * 注意：esp_restart() 前需确认不在 TWDT 临界区（设计文档 §5.6）。 */
}

/* ============================ 重启 ============================ */
static void p_reboot(void)
{
    /* 重启前确保 NVS 已提交（由调用方保证），这里留时间让日志刷出 */
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

/* ============================ 按键输入 ======================== */
/* raw[] 下标与 app/key_fsm.h 的 VK_IDX_* 一一对应 */
static const gpio_num_t s_key_gpio[VK_RAW_INPUT_COUNT] = {
    VK_BOARD_KEY1_GPIO, VK_BOARD_KEY2_GPIO, VK_BOARD_KEY3_GPIO,
    VK_BOARD_KEY4_GPIO, VK_BOARD_KEY5_GPIO, VK_BOARD_KEY6_GPIO,
    VK_BOARD_KEY7_GPIO,
    VK_BOARD_ENC_A_GPIO,  VK_BOARD_ENC_B_GPIO,  VK_BOARD_ENC_SW_GPIO
};

static void p_keys_sample(uint8_t raw[VK_RAW_INPUT_COUNT])
{
    unsigned i;
    for (i = 0u; i < VK_RAW_INPUT_COUNT; i++) {
        /* 按键低有效：读到 0 表示按下 -> 上报 1 */
        raw[i] = (uint8_t)(gpio_get_level(s_key_gpio[i]) == 0 ? 1u : 0u);
    }
}

/* ============================ LED ============================= */
static void p_led_set(uint8_t blue_on, uint8_t red_on)
{
    gpio_set_level((gpio_num_t)VK_BOARD_LED_BLUE_GPIO, (uint32_t)(blue_on ? 1 : 0));
    gpio_set_level((gpio_num_t)VK_BOARD_LED_RED_GPIO, (uint32_t)(red_on ? 1 : 0));
}

/* ============================ UART1 调试 ====================== */
static void p_uart1_print(const char *line)
{
    if (line == NULL) {
        return;
    }
    uart_write_bytes((uart_port_t)1, line, (size_t)strlen(line));
    uart_write_bytes((uart_port_t)1, "\r\n", 2);
}

/* ============================ USB CDC ========================= */
/*
 * VID/PID 冻结在设计文档 §7.1：
 *   工作态 TinyUSB CDC  -> 303A:4001
 *   下载模式 USB Serial/JTAG -> 303A:1001
 * 上位机按这两对值区分设备、并处理烧录时的 COM 口跳变。
 */
#define VK_USB_VID          0x303Au
#define VK_USB_PID          0x4001u
#define VK_USB_BCD          0x0200u   /* USB 2.0 */
#define VK_USB_EP0_SIZE     64u

/* CDC 用的端点：0x81 通知(IN)、0x02 数据(OUT)、0x82 数据(IN) */
#define VK_USB_CDC_EP_NOTIF 0x81u
#define VK_USB_CDC_EP_OUT   0x02u
#define VK_USB_CDC_EP_IN    0x82u
#define VK_USB_CDC_EP_SIZE  64u       /* 全速下 bulk 包长 */

#define VK_CDC_ITF          0u        /* 只有一个 CDC 实例 */

/* 一次从 CDC 读上来的最大字节数。取 256 而不是端点大小，
 * 因为一帧最多 1032 字节，多读几次由 FrameStream 累积。 */
#define VK_USB_RX_CHUNK     256u

/**
 * 设备描述符。
 * bDeviceClass/SubClass/Protocol 用 MISC/IAD 组合——这是 CDC 复合设备的标准写法，
 * 真正的类信息在各个接口描述符里。
 */
static const tusb_desc_device_t s_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = VK_USB_BCD,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = VK_USB_EP0_SIZE,
    .idVendor = VK_USB_VID,
    .idProduct = VK_USB_PID,
    .bcdDevice = 0x0100u,
    .iManufacturer = 0x01u,
    .iProduct = 0x02u,
    .iSerialNumber = 0x03u,
    .bNumConfigurations = 0x01u
};

/** 配置描述符 + 接口 + 端点。接口数 2：CDC 控制接口 + CDC 数据接口。 */
static const uint8_t s_desc_config[] = {
    /* 配置号、接口数、字符串索引、总长度、属性、供电(mA) */
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, (9 + TUD_CDC_DESC_LEN), TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    /* 接口号、字符串索引、通知端点、通知端点大小、OUT 端点、IN 端点、端点大小 */
    TUD_CDC_DESCRIPTOR(VK_CDC_ITF, 0, VK_USB_CDC_EP_NOTIF, 8, VK_USB_CDC_EP_OUT, VK_USB_CDC_EP_IN, VK_USB_CDC_EP_SIZE)
};

/** 字符串描述符数组。索引 0 固定为语言 ID。 */
static const char *const s_string_desc[] = {
    0,                       /* 0: 语言 ID（由框架处理） */
    "Yingli Technology",     /* 1: 厂商 */
    "VibeKey",               /* 2: 产品 */
    "VK-0001",               /* 3: 序列号（TODO 用 MAC 填） */
    "VibeKey CDC"            /* 4: CDC 接口 */
};

/** TinyUSB 描述符回调：新版 API 用回调而不是静态配置表。 */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_desc_config;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t buf[32];
    uint8_t len;

    if (index == 0u) {
        buf[1] = ((uint16_t)langid & 0xFFu) << 8;
        buf[0] = (uint16_t)((uint16_t)(TUSB_DESC_STRING << 8) | 4u);
        return buf;
    }

    if (index >= (sizeof(s_string_desc) / sizeof(s_string_desc[0]))) {
        return NULL;
    }

    const char *s = s_string_desc[index];
    len = (uint8_t)strlen(s);
    if (len > 31u) {
        len = 31u;
    }
    for (uint8_t i = 0u; i < len; i++) {
        buf[1u + i] = (uint16_t)s[i];
    }
    buf[0] = (uint16_t)((uint16_t)(TUSB_DESC_STRING << 8) | (uint16_t)(2u * len + 2u));
    return buf;
}

/* 前向声明：USB 任务在 vk_usb_poll 定义之前就要用到它 */
void vk_usb_poll(void);

/**
 * USB 任务：驱动 TinyUSB 协议栈并泵上行数据。
 *
 * 放在独立任务里而不是主循环里，原因有两个：
 *   1. TinyUSB 要求 tud_task() 被高频调用，而主循环有 5ms 的按键轮询节拍；
 *   2. 上位机的下行指令要及时响应，不能被语音 ASR 的阻塞调用拖住。
 */
static void vk_usb_task(void *arg)
{
    (void)arg;
    for (;;) {
        vk_usb_poll();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void p_usb_init(const port_esp32_hooks_t *hooks)
{
    /* 角色：设备（DEVICE）。速度由 Kconfig 决定（全速 12Mbps）。 */
    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    if (!tusb_rhport_init(0, &rh_init)) {
        ESP_LOGE(TAG, "tusb_rhport_init 失败");
        return;
    }

    xTaskCreate(vk_usb_task, "vk_usb", 4096, NULL, 5, NULL);
}

/** 从 CDC 读数据并转交应用层。 */
static void p_usb_read_pump(void)
{
    uint8_t buf[VK_USB_RX_CHUNK];

    if (!tud_cdc_n_connected(VK_CDC_ITF)) {
        return;
    }

    while (tud_cdc_n_available(VK_CDC_ITF) > 0u) {
        uint32_t n = tud_cdc_n_read(VK_CDC_ITF, buf, sizeof(buf));
        if (n == 0u) {
            break;
        }
        if ((s_hooks.on_usb_bytes != NULL) && (s_hooks.app != NULL)) {
            s_hooks.on_usb_bytes(s_hooks.app, buf, n);
        }
    }
}

/**
 * 供 USB 任务调用：先推进协议栈，再把收到的字节泵给应用层。
 * 两步必须紧邻——tud_task() 内部才会真正接收数据，
 * 分开调用会漏掉刚到的字节。
 */
void vk_usb_poll(void)
{
    tud_task();
    p_usb_read_pump();
}

/* ============================ 帧发送 ========================== */

/* 云 WebSocket 客户端句柄与在线标志（在 vk_cloud_task 段初始化）。
 * 挪到前面声明，因为 p_send_frame 要用它。 */
static esp_websocket_client_handle_t s_cloud_client;
static volatile int s_cloud_online;

/* TCP 客户端 socket（-1 表示没客户端）。挪到前面声明，p_send_frame 要用。 */
static volatile int s_tcp_client = -1;

/* 前向声明：vk_cloud_task 要用，定义在后面。 */
int port_esp32_wifi_ready(void);

static void p_send_frame(const uint8_t *encoded, size_t len)
{
    if ((encoded == NULL) || (len == 0u)) {
        return;
    }
    if (!tud_cdc_n_connected(VK_CDC_ITF)) {
        return;   /* 上位机没开串口，直接丢弃 */
    }

    /* write 可能只写进去一部分，循环补写；每次补写后 flush 让数据尽快上网线 */
    size_t off = 0u;
    while (off < len) {
        uint32_t n = tud_cdc_n_write(VK_CDC_ITF, &encoded[off], (uint32_t)(len - off));
        if (n == 0u) {
            break;   /* 缓冲区满，等下一轮 */
        }
        off += n;
    }
    tud_cdc_n_write_flush(VK_CDC_ITF);

    /* WiFi TCP 通道：有客户端连着就发（设计文档 §3.4 规则 2：同一 seq 双通道同发） */
    if (s_tcp_client >= 0) {
        size_t off2 = 0u;
        while (off2 < len) {
            int n = send(s_tcp_client, &encoded[off2], len - off2, 0);
            if (n <= 0) {
                close(s_tcp_client);
                s_tcp_client = -1;
                break;
            }
            off2 += (size_t)n;
        }
    }

    /* 云 WebSocket 通道：云在线就发 */
    if (s_cloud_client != NULL) {
        (void)esp_websocket_client_send_bin(s_cloud_client,
                                            (const char *)encoded, (int)len,
                                            pdMS_TO_TICKS(200));
    }
}

/* 说明：这里故意没有"下行帧入口"。
 * 移植层只向上交字节（hooks.on_usb_bytes），帧解码与分发由 app 层自己做
 * ——vk_app_on_usb_bytes() 内部会解码并调用 vk_app_on_frame()。
 * 这样 SEQ 跨通道去重才有统一视图（设计文档 §3.4 规则 3）。 */

/* ============================ 网络字节队列 ===================== */
/*
 * 为什么需要它：网络任务（TCP / 云 WebSocket）不能直接调 hooks.on_wifi_bytes，
 * 那会和主循环（vk_app_poll）并发访问 app 状态（已存在的数据竞争，
 * 云连接加进来后竞争概率更高）。
 *
 * 做法：网络任务只把收到的字节放进这个队列，应用层在 vk_app_poll 里调
 * net_poll，由 net_poll 在**应用层任务上下文里** drain 并调 on_wifi_bytes。
 * 这样 app 层只在主循环任务里被调用，线程安全（见 port.h net_poll 注释）。
 */

#define VK_NET_QUEUE_DEPTH  16u
#define VK_NET_CHUNK_MAX    256u

typedef struct {
    uint8_t  data[VK_NET_CHUNK_MAX];
    uint16_t len;
} net_chunk_t;

static QueueHandle_t s_net_queue;

/** 网络任务把一段字节放进队列（供 vk_tcp_task / vk_cloud_task 调用）。 */
static void net_enqueue(const uint8_t *data, size_t len)
{
    net_chunk_t chunk;
    size_t off = 0u;

    if ((data == NULL) || (len == 0u) || (s_net_queue == NULL)) {
        return;
    }
    while (off < len) {
        size_t n = len - off;
        if (n > VK_NET_CHUNK_MAX) {
            n = VK_NET_CHUNK_MAX;
        }
        memcpy(chunk.data, &data[off], n);
        chunk.len = (uint16_t)n;
        /* 队列满就丢（xQueueSend 超时 0），避免网络任务被阻塞 */
        if (xQueueSend(s_net_queue, &chunk, 0) != pdTRUE) {
            break;
        }
        off += n;
    }
}

/** net_poll：在应用层上下文里 drain 队列并调 hooks.on_wifi_bytes。 */
static void p_net_poll(void)
{
    net_chunk_t chunk;
    if (s_net_queue == NULL) {
        return;
    }
    while (xQueueReceive(s_net_queue, &chunk, 0) == pdTRUE) {
        if ((s_hooks.on_wifi_bytes != NULL) && (s_hooks.app != NULL)) {
            s_hooks.on_wifi_bytes(s_hooks.app, chunk.data, (uint32_t)chunk.len);
        }
    }
}

/* ============================ NVS ============================= */
static nvs_handle_t s_nvs = 0;

static int p_nvs_set_str(const char *key, const char *val)
{
    if (s_nvs == 0) {
        return VK_PORT_FAIL;
    }
    return (nvs_set_str(s_nvs, key, val) == ESP_OK) ? VK_PORT_OK : VK_PORT_FAIL;
}

static int p_nvs_get_str(const char *key, char *out, size_t cap)
{
    size_t need = cap;
    esp_err_t e;
    if (s_nvs == 0) {
        return VK_PORT_FAIL;
    }
    e = nvs_get_str(s_nvs, key, out, &need);
    if (e == ESP_OK) {
        return VK_PORT_OK;
    }
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        return VK_PORT_NOENT;
    }
    return VK_PORT_FAIL;
}

static int p_nvs_commit(void)
{
    if (s_nvs == 0) {
        return VK_PORT_FAIL;
    }
    return (nvs_commit(s_nvs) == ESP_OK) ? VK_PORT_OK : VK_PORT_FAIL;
}

static int p_nvs_erase_all(void)
{
    if (s_nvs == 0) {
        return VK_PORT_FAIL;
    }
    return (nvs_erase_all(s_nvs) == ESP_OK) ? VK_PORT_OK : VK_PORT_FAIL;
}

/* ============================ WiFi ============================ */
/*
 * 设计文档 §5.2 / §6.2：
 *   - AP 模式：SSID = VibeKey-<MAC后4位>，WPA2-PSK 加密，等上位机来下发家庭 WiFi
 *   - STA 模式：用 NVS 里的凭据连路由器
 *   - 无论哪种模式都跑两个服务端口：
 *       TCP  CONFIG_VIBEKEY_TCP_PORT       (8266)  业务帧收发
 *       UDP  CONFIG_VIBEKEY_DISCOVERY_PORT (8267)  设备发现广播应答
 *
 * 两个服务任务在 port_esp32_init 里一次性创建，之后常驻。
 * AP/STA 切换只改 WiFi 模式，不动 socket——这样上位机不需要重新发现一次。
 */

static esp_netif_t *s_netif_ap;
static esp_netif_t *s_netif_sta;
static volatile int s_wifi_started;



/** 退出当前 WiFi 模式，便于切到另一种模式。 */
static void wifi_teardown(void)
{
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = 0;
    }
}

static int p_wifi_start_ap(const char *ssid, const char *pass)
{
    if ((ssid == NULL) || (pass == NULL)) {
        return VK_PORT_FAIL;
    }
    /* WPA2 要求密码至少 8 位，否则 ESP-IDF 会拒绝启动 AP。
     * 提前拦下来并返回失败，让状态机能走"重试或回退"的正常路径。 */
    if (strlen(pass) < 8) {
        ESP_LOGE(TAG, "AP 密码不足 8 位，拒绝启动");
        return VK_PORT_FAIL;
    }

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    wifi_teardown();

    strncpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid) - 1);
    strncpy((char *)cfg.ap.password, pass, sizeof(cfg.ap.password) - 1);
    cfg.ap.ssid_len = (uint8_t)strnlen(ssid, sizeof(cfg.ap.ssid));
    cfg.ap.max_connection = (uint8_t)CONFIG_VIBEKEY_AP_MAX_CONN;
    /* PRD 5.2 明确要求 AP 加密，不能开开放式热点 */
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t e = esp_wifi_set_mode(WIFI_MODE_AP);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "set_mode AP 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }
    e = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "set_config AP 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }
    e = esp_wifi_start();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start AP 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }
    s_wifi_started = 1;
    ESP_LOGI(TAG, "AP 已启动: %s", ssid);
    return VK_PORT_OK;
}

static int p_wifi_start_sta(const char *ssid, const char *pass)
{
    if ((ssid == NULL) || (pass == NULL)) {
        return VK_PORT_FAIL;
    }

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    wifi_teardown();

    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    /* 阈值设成 WPA2：比它弱的认证方式直接不连，避免连到开放式假冒热点 */
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "set_mode STA 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }
    e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "set_config STA 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }
    e = esp_wifi_start();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start STA 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }
    s_wifi_started = 1;

    /* 立即发起连接。连接结果不在这里等——由 vk_net_fsm 通过
     * p_wifi_ip() 是否拿到 IP 来判断成功与否（设计文档 §5.2）。 */
    e = esp_wifi_connect();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "wifi_connect 返回 %s（交由状态机重试）", esp_err_to_name(e));
    }
    ESP_LOGI(TAG, "STA 启动，尝试连接 %s", ssid);
    return VK_PORT_OK;
}

static void p_wifi_stop(void)
{
    if (s_tcp_client >= 0) {
        close(s_tcp_client);
        s_tcp_client = -1;
    }
    wifi_teardown();
}

static wifi_mode_t p_wifi_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;

    if (s_wifi_started == 0) {
        return WIFI_MODE_NULL;
    }
    /* v5.5 的签名是 esp_err_t esp_wifi_get_mode(wifi_mode_t *)——
     * 出参在指针里，返回值只是错误码，不要直接 return 它。 */
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return WIFI_MODE_NULL;
    }
    return mode;
}

static int p_wifi_rssi(void)
{
    wifi_ap_record_t ap;

    if (p_wifi_mode() != WIFI_MODE_STA) {
        return 0;
    }
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    return ap.rssi;
}

static int p_wifi_ip(char *out, size_t cap)
{
    esp_netif_ip_info_t ip;

    if ((out == NULL) || (cap == 0u)) {
        return VK_PORT_FAIL;
    }
    out[0] = '\0';

    if (s_wifi_started == 0) {
        return VK_PORT_OK;
    }

    /* AP 模式固定 192.168.4.1；STA 模式取 DHCP 分配到的地址 */
    esp_netif_t *nif = (p_wifi_mode() == WIFI_MODE_STA) ? s_netif_sta : s_netif_ap;
    if ((nif == NULL) || (esp_netif_get_ip_info(nif, &ip) != ESP_OK)) {
        return VK_PORT_OK;
    }
    if (ip.ip.addr == 0) {
        return VK_PORT_OK;   /* 还没拿到 IP（STA 正在关联） */
    }

    /* ip.ip 是 esp_ip4_addr_t，lwip 的 ip4addr_ntoa_r 要 const ip4_addr_t * */
    ip4addr_ntoa_r((const ip4_addr_t *)&ip.ip, out, (int)cap);
    return VK_PORT_OK;
}

/** 把 STA 连上的路由器名回给状态机（用于状态帧里的 SSID= 字段）。 */
int port_esp32_get_sta_ssid(char *out, size_t cap)
{
    wifi_ap_record_t ap;

    if ((out == NULL) || (cap == 0u)) {
        return VK_PORT_FAIL;
    }
    out[0] = '\0';
    if (p_wifi_mode() != WIFI_MODE_STA) {
        return VK_PORT_OK;
    }
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return VK_PORT_OK;
    }
    strncpy(out, (const char *)ap.ssid, cap - 1);
    out[cap - 1] = '\0';
    return VK_PORT_OK;
}

/* ---------------- UDP 发现服务 ---------------- */

/**
 * UDP 发现任务：应答上位机的广播查询。
 *
 * 为什么需要它：STA 模式下 PC 连的是路由器，设备 IP 由 DHCP 分配，
 * PC 无从得知。让设备在收到广播后自报家门（设计文档 §6.2）。
 * 这是 PRD 缺失的一环，没有它用户每次都得查路由器后台。
 */
static void vk_udp_task(void *arg)
{
    char rx[64];
    char reply[96];

    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "UDP socket 创建失败");
        vTaskDelete(NULL);
        return;
    }

    /* 必须设 SO_REUSEADDR，否则模式切换后重新 bind 会 EADDRINUSE */
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)CONFIG_VIBEKEY_DISCOVERY_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "UDP bind %d 失败", CONFIG_VIBEKEY_DISCOVERY_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP 发现服务已监听 %d", CONFIG_VIBEKEY_DISCOVERY_PORT);

    for (;;) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        int n = recvfrom(sock, rx, sizeof(rx) - 1, 0, (struct sockaddr *)&from, &fromlen);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        rx[n] = '\0';

        /* 上位机发 "VIBEKEY_DISCOVER"，设备回 "VIBEKEY_HERE,IP=...,MAC=...,FW=..." */
        if (strstr(rx, "VIBEKEY_DISCOVER") == NULL) {
            continue;
        }

        char ip[16] = "";
        p_wifi_ip(ip, sizeof(ip));

        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);

        int len = snprintf(reply, sizeof(reply),
                           "VIBEKEY_HERE,IP=%s,MAC=%02X%02X%02X%02X%02X%02X,FW=%s",
                           ip, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                           CONFIG_VIBEKEY_FW_VERSION);
        if (len > 0) {
            sendto(sock, reply, (size_t)len, 0, (struct sockaddr *)&from, fromlen);
        }
    }
}

/* ---------------- TCP 服务：业务帧收发 ---------------- */

/**
 * TCP 服务任务：只服务一个客户端（本期不支持多设备，PRD 8 章）。
 *
 * 收到字节立刻转交 app 层；app 层自己解码、去重、分发。
 * 这里刻意不做任何协议解析——保持"移植层只交字节"的分层。
 */
static void vk_tcp_task(void *arg)
{
    uint8_t buf[256];

    (void)arg;

    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (srv < 0) {
        ESP_LOGE(TAG, "TCP socket 创建失败");
        vTaskDelete(NULL);
        return;
    }

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)CONFIG_VIBEKEY_TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "TCP bind %d 失败", CONFIG_VIBEKEY_TCP_PORT);
        close(srv);
        vTaskDelete(NULL);
        return;
    }
    if (listen(srv, 1) != 0) {
        ESP_LOGE(TAG, "TCP listen 失败");
        close(srv);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP 服务已监听 %d", CONFIG_VIBEKEY_TCP_PORT);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);

        int cli = accept(srv, (struct sockaddr *)&peer, &peerlen);
        if (cli < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* 已有客户端时拒绝新连接，而不是踢掉旧的——
         * 踢掉旧客户端会让上位机莫名其妙掉线，拒绝新的更可预期。 */
        if (s_tcp_client >= 0) {
            ESP_LOGW(TAG, "已有客户端连接，拒绝新连接");
            close(cli);
            continue;
        }

        s_tcp_client = cli;
        ESP_LOGI(TAG, "TCP 客户端已连接");

        for (;;) {
            int n = recv(cli, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;   /* 对方关闭或出错 */
            }
            /* 放队列，由 net_poll 在应用层上下文调 app——不在这里直接调
             * （修已存在的并发问题：网络任务与主循环竞争 app 状态） */
            net_enqueue(buf, (size_t)n);
        }

        close(cli);
        s_tcp_client = -1;
        ESP_LOGI(TAG, "TCP 客户端已断开");
    }
}

/* ============================ 云 WebSocket 客户端 ==================== */
/*
 * 阶段 2：设备主动连云。
 *
 * 设备连上 WiFi（WORK_READY）后，读 NVS 的 cloud_url / cloud_token，
 * 主动连 wss://<cloud_url>?token=<cloud_token>，保持长连接。
 * 浏览器经服务器的 WebSocket 桥配对转发（web/server/bridge.js）。
 *
 * 收到的二进制帧 -> net_enqueue（由 net_poll 在应用层上下文调 app）。
 * 发送 -> p_send_frame 里 esp_websocket_client_send_bin。
 *
 * 认证：cloud_token 由用户在网页端生成，配网时通过 0x12 ASR 配置帧
 * 的扩展键（cloud_url / cloud_token）下发并存 NVS。
 */

#define VK_CLOUD_URL_MAX  160u
#define VK_CLOUD_TOKEN_MAX 64u

/** 从 NVS 读云配置。返回 1 表示两项都有。 */
static int cloud_load_config(char *url, size_t url_cap, char *token, size_t token_cap)
{
    int ok = 0;
    ok += (p_nvs_get_str("cloud_url", url, url_cap) == VK_PORT_OK) && (url[0] != '\0');
    ok += (p_nvs_get_str("cloud_token", token, token_cap) == VK_PORT_OK) && (token[0] != '\0');
    return (ok == 2) ? 1 : 0;
}

/** 云事件回调：把收到的二进制帧放进网络队列。 */
static void cloud_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)args;
    (void)base;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_cloud_online = 1;
        ESP_LOGI(TAG, "云已连接");
        break;

    case WEBSOCKET_EVENT_DATA:
        if ((data->data_ptr != NULL) && (data->data_len > 0)) {
            net_enqueue((const uint8_t *)data->data_ptr, (size_t)data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        s_cloud_online = 0;
        ESP_LOGW(TAG, "云断开");
        break;

    case WEBSOCKET_EVENT_ERROR:
        s_cloud_online = 0;
        ESP_LOGE(TAG, "云错误");
        break;

    default:
        break;
    }
}

/** 云连接任务：连上 WiFi 后主动连云，断开自动重连。 */
static void vk_cloud_task(void *arg)
{
    char url[VK_CLOUD_URL_MAX];
    char token[VK_CLOUD_TOKEN_MAX];
    char full_url[VK_CLOUD_URL_MAX + VK_CLOUD_TOKEN_MAX + 16];

    (void)arg;

    for (;;) {
        /* 等设备进入 WORK_READY（WiFi 已连上） */
        if (!port_esp32_wifi_ready()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (cloud_load_config(url, sizeof(url), token, sizeof(token)) == 0) {
            /* 还没配云（没收到 cloud_url / cloud_token），等配置 */
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* 拼完整 URL：cloud_url + "?token=" + cloud_token */
        int n = snprintf(full_url, sizeof(full_url), "%s?token=%s", url, token);
        if (n <= 0 || (size_t)n >= sizeof(full_url)) {
            ESP_LOGE(TAG, "云 URL 过长");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "连云：%s?token=%s…", url, token);

        esp_websocket_client_config_t cfg = {
            .uri = full_url,
            .use_global_ca_store = true,
            .disable_auto_reconnect = true,   /* 重连由本任务自己控制（带退避） */
            .task_stack = 6144,
            .task_prio = 5,
        };

        s_cloud_client = esp_websocket_client_init(&cfg);
        if (s_cloud_client == NULL) {
            ESP_LOGE(TAG, "云 client init 失败");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        esp_websocket_register_events(s_cloud_client, WEBSOCKET_EVENT_ANY,
                                      cloud_event_handler, NULL);

        /* start 是阻塞的（内部跑事件循环），断开或出错才返回 */
        esp_err_t e = esp_websocket_client_start(s_cloud_client);
        s_cloud_online = 0;
        ESP_LOGW(TAG, "云连接结束：%s，5s 后重试", esp_err_to_name(e));

        esp_websocket_client_destroy(s_cloud_client);
        s_cloud_client = NULL;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/** 供 app 层查询云通道是否在线。 */
int port_esp32_cloud_online(void)
{
    return s_cloud_online;
}

/** 供 app 层查询 WiFi 通道是否可用（用于状态帧里的 CH= 字段）。 */
int port_esp32_wifi_ready(void)
{
    return (s_wifi_started != 0) ? 1 : 0;
}

/* ============================ ASR：腾讯云实时语音识别 ============================ */
/*
 * 硬件已定为 TDK InvenSense ICS-43434（I2S 数字麦，BOM 第 41 项）。
 * ASR 对接（设计文档 §5.5）：
 *   流式  -> 腾讯云实时语音识别（WebSocket），按住说话边说边出字
 *   整包  -> 腾讯云一句话识别（HTTP POST，≤60s），录完整段一次性上传
 *
 * 鉴权：AppID / SecretID / SecretKey 由上位机写入 NVS（0x12 帧，键见 cmd_dispatch.h）。
 * 签名用腾讯云标准 TC3 签名（与其他云 API 一致）：对 host+timestamp+payload
 * 算 HMAC-SHA256(SecretKey, ...)，拼进 URL 的 query 里。
 * 密钥绝对不进日志（PRD 9 章）。
 *
 * 音频参数冻结：16 kHz / 16 bit / 单声道 PCM。
 *
 * 当前状态：I2S 采集与 WebSocket 链路已实现；腾讯云协议的完整握手/签名/分帧
 * 已按公开文档实现，但只能在有真实凭据和外网的环境里端到端联调。
 */

#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "esp_heap_caps.h"

/* ---------------- I2S 采集 ---------------- */

static i2s_chan_handle_t s_i2s_rx;
static int s_mic_ready;

#define VK_MIC_FRAME_BYTES  960u          /* 30ms @16kHz 单声道 16bit */
#define VK_MIC_READ_BYTES   (VK_MIC_FRAME_BYTES * 16u)

static int p_mic_init(void)
{
    if (VK_BOARD_MIC_TYPE != VK_MIC_TYPE_I2S) {
        ESP_LOGE(TAG, "port_board.h 的 VK_BOARD_MIC_TYPE 不是 I2S，无法初始化");
        return VK_PORT_FAIL;
    }
    if (s_mic_ready != 0) {
        return VK_PORT_OK;
    }

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_AUTO,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 240,
    };
    esp_err_t e = i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel 失败: %s", esp_err_to_name(e));
        return VK_PORT_FAIL;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = (gpio_num_t)VK_BOARD_I2S_BCK_GPIO,
            .ws   = (gpio_num_t)VK_BOARD_I2S_WS_GPIO,
            .dout = GPIO_NUM_NC,
            .din  = (gpio_num_t)VK_BOARD_I2S_DIN_GPIO,
        },
    };

    e = i2s_channel_init_std_mode(s_i2s_rx, &std_cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode 失败: %s", esp_err_to_name(e));
        i2s_del_channel(s_i2s_rx);
        s_i2s_rx = NULL;
        return VK_PORT_FAIL;
    }

    s_mic_ready = 1;
    ESP_LOGI(TAG, "I2S 麦克风就绪（16kHz/16bit/单声道）");
    return VK_PORT_OK;
}

static size_t p_mic_read(uint8_t *buf, size_t cap, uint32_t timeout_ms)
{
    size_t n = 0;
    if ((s_mic_ready == 0) || (buf == NULL) || (cap == 0u)) {
        return 0u;
    }
    esp_err_t e = i2s_channel_read(s_i2s_rx, buf, cap, &n, timeout_ms);
    return (e == ESP_OK) ? n : 0u;
}

/* ---------------- 腾讯云 TC3 签名 ---------------- */

/*
 * 腾讯云实时语音识别的 WebSocket 握手是标准 HTTPS GET 请求，URL 的 query
 * 里带签名。签名算法（TC3-HMAC-SHA256）：
 *   stringToSign = HTTP_METHOD\nPATH\nQUERY\nHEADERS\nSIGNED_HEADERS\nHASHED_PAYLOAD
 *   其中 HASHED_PAYLOAD = SHA256(payload) 的 hex 小写
 *   signature = HMAC_SHA256(SecretKey, stringToSign) 的 hex 小写
 *
 * 实时语音识别（asr.cloud.tencent.com）的握手是特殊的"语音识别专用"简化版：
 *   GET wss://asr.cloud.tencent.com/asr/v2/{appid}?secretid=..&ts=..&nonce=..&signature=..
 * 其中 signature = HMAC_SHA256(SecretKey, "GETasr.cloud.tencent.com/asr/v2/{appid}?secretid=..&ts=..&nonce=..")
 * 的 base64（不是 hex）。这个简化版不需要 TC3 的完整 7 字段拼接。
 *
 * 这里实现的是腾讯云实时语音识别文档里的简化签名。
 */

/**
 * 用 SecretKey 对一段字符串算 HMAC-SHA256，输出 base64。
 * 用于腾讯云实时语音识别的握手签名。
 */
static void tencent_sign_hmac_b64(const char *secret_key, const char *data,
                                  char *out, size_t out_cap)
{
    uint8_t mac[32];
    size_t mac_len = sizeof(mac);

    /* v5.5 的 mbedtls 没有 mbedtls_md_hmac_sha256 这个便捷函数，
     * 用 starts/update/finish 三步调用。 */
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t *)secret_key, strlen(secret_key));
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)data, strlen(data));
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);

    size_t b64_len = 0;
    mbedtls_base64_encode((uint8_t *)out, out_cap, &b64_len, mac, mac_len);
    out[b64_len] = '\0';
}

/** 生成 32 位随机数作为 nonce。 */
static uint32_t tencent_nonce(void)
{
    uint32_t v;
    esp_fill_random(&v, sizeof(v));
    return v;
}

/* ---------------- ASR 会话 ---------------- */

static esp_websocket_client_handle_t s_ws;
static int s_asr_running;
static uint8_t s_asr_mode;

/** 从 NVS 读 ASR 凭据。返回 0 表示四项都拿到了。 */
static int asr_load_creds(char *appid, size_t appid_cap,
                          char *sid, size_t sid_cap,
                          char *skey, size_t skey_cap,
                          char *provider, size_t provider_cap)
{
    int ok = 0;
    ok += (p_nvs_get_str("asr_appid", appid, appid_cap) == VK_PORT_OK);
    ok += (p_nvs_get_str("asr_sid", sid, sid_cap) == VK_PORT_OK);
    ok += (p_nvs_get_str("asr_skey", skey, skey_cap) == VK_PORT_OK);
    ok += (p_nvs_get_str("asr_provider", provider, provider_cap) == VK_PORT_OK);
    return (ok == 4) ? 0 : -1;
}

/**
 * WebSocket 事件回调：把服务端返回的转写文本转给 app 层。
 *
 * 腾讯云实时语音识别的响应是 JSON 文本帧，形如
 *   {"code":0,"message":"...","result":{"slice_type":2,"index":0,"start_time":..,"end_time":..,"voice_text_str":"你好"}}
 * slice_type：0=一句话开始，1=中间结果，2=最终结果，4=一句话结束
 * 我们不需要解析整个 JSON——只需要把 voice_text_str 和 slice_type 拿出来，
 * 转成 app 层的 0x03 帧（is_final = slice_type >= 2）。
 *
 * 用极简的字符串查找而不是完整 JSON 解析器：ESP-IDF 的 cJSON 太重，
 * 而且这个字段结构非常稳定，查找两个已知键名就够了。
 */
static int json_find_str(const char *json, const char *key, char *out, size_t out_cap)
{
    const char *p = strstr(json, key);
    if (p == NULL) {
        return -1;
    }
    p += strlen(key);
    /* 跳过 " : " 到第一个引号 */
    while (*p && *p != '"') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    size_t n = 0u;
    while (p[n] && p[n] != '"' && (n + 1u) < out_cap) {
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
    return (n > 0u) ? 0 : -1;
}

static int json_find_int(const char *json, const char *key, int *out)
{
    const char *p = strstr(json, key);
    if (p == NULL) {
        return -1;
    }
    p += strlen(key);
    while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) {
        p++;
    }
    if (*p == '\0') {
        return -1;
    }
    *out = (int)strtol(p, NULL, 10);
    return 0;
}

/** 把一条腾讯云结果转给 app 层。由 ws 事件回调调用。 */
static void asr_forward_result(const char *json)
{
    int slice = -1;
    char text[512];

    if (json_find_int(json, "\"slice_type\"", &slice) != 0) {
        return;
    }
    if (json_find_str(json, "\"voice_text_str\"", text, sizeof(text)) != 0) {
        return;
    }

    /* slice_type: 0=一句话开始, 1=中间, 2=一句话结束, 3=整句结束 */
    int is_final = (slice >= 2) ? 1 : 0;

    if (s_hooks.app != NULL && s_hooks.on_asr_text != NULL) {
        s_hooks.on_asr_text(s_hooks.app, text, (uint8_t)is_final);
    }
}

/** WebSocket 事件回调。 */
static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    (void)handler_args;
    (void)base;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "ASR WebSocket 已连接");
        break;

    case WEBSOCKET_EVENT_DATA:
        /* 文本帧：转写结果 */
        if (data->data_ptr != NULL && data->data_len > 0) {
            /* 空字符结尾不一定可靠，先复制到定长缓冲再加 \0 */
            char json[1024];
            size_t n = (size_t)data->data_len;
            if (n >= sizeof(json)) {
                n = sizeof(json) - 1u;
            }
            memcpy(json, data->data_ptr, n);
            json[n] = '\0';
            asr_forward_result(json);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ASR WebSocket 断开");
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "ASR WebSocket 错误");
        break;

    default:
        break;
    }
}

/**
 * 开始 ASR 会话。
 *
 * 流式：连 wss://asr.cloud.tencent.com/asr/v2/{appid}，握手后开始推流。
 * 整包：只启动采集缓存，停止时一次性 POST 到一句话识别接口。
 */
static int p_asr_start(uint8_t mode)
{
    if (s_asr_running != 0) {
        return VK_PORT_OK;
    }

    if (p_mic_init() != VK_PORT_OK) {
        return VK_PORT_FAIL;
    }

    /* 读凭据 */
    char appid[VK_ASR_APPID_MAX];
    char sid[VK_ASR_SECRET_MAX];
    char skey[VK_ASR_SECRET_MAX];
    char provider[VK_ASR_PROVIDER_MAX];

    if (asr_load_creds(appid, sizeof(appid), sid, sizeof(sid),
                       skey, sizeof(skey), provider, sizeof(provider)) != 0) {
        ESP_LOGE(TAG, "ASR 凭据未配置（NVS 里没有 appid/secret_id/secret_key）");
        return VK_PORT_FAIL;
    }

    if (i2s_channel_enable(s_i2s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable 失败");
        return VK_PORT_FAIL;
    }

    s_asr_mode = mode;

    if (mode == VK_ASR_MODE_STREAM) {
        /* ---- 流式：建 WebSocket 连接 ---- */
        uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000);
        uint32_t nonce = tencent_nonce();

        /* 简化签名字符串：GET + host + path + ?secretid=..&ts=..&nonce=.. */
        char to_sign[512];
        int n = snprintf(to_sign, sizeof(to_sign),
                         "GETasr.cloud.tencent.com/asr/v2/%s?secretid=%s&ts=%u&nonce=%u",
                         appid, sid, (unsigned)ts, (unsigned)nonce);
        if (n <= 0 || (size_t)n >= sizeof(to_sign)) {
            ESP_LOGE(TAG, "签名串过长");
            i2s_channel_disable(s_i2s_rx);
            return VK_PORT_FAIL;
        }

        char sig_b64[64];
        tencent_sign_hmac_b64(skey, to_sign, sig_b64, sizeof(sig_b64));

        /* 注意：sig_b64 里可能含 + / = 等需要 URL 编码的字符。
         * 腾讯云文档明确要求 signature 先做 base64 再 URL 编码（percent-encode）。
         * 这里用极简的只编码这 3 个字符的做法。 */
        char sig_enc[96];
        size_t ei = 0u;
        for (size_t i = 0u; sig_b64[i] && (ei + 3u) < sizeof(sig_enc); i++) {
            char c = sig_b64[i];
            if (c == '+') {
                sig_enc[ei++] = '%'; sig_enc[ei++] = '2'; sig_enc[ei++] = 'B';
            } else if (c == '/') {
                sig_enc[ei++] = '%'; sig_enc[ei++] = '2'; sig_enc[ei++] = 'F';
            } else if (c == '=') {
                sig_enc[ei++] = '%'; sig_enc[ei++] = '3'; sig_enc[ei++] = 'D';
            } else {
                sig_enc[ei++] = c;
            }
        }
        sig_enc[ei] = '\0';

        char uri[768];
        n = snprintf(uri, sizeof(uri),
                     "wss://asr.cloud.tencent.com/asr/v2/%s?secretid=%s&ts=%u&nonce=%u&signature=%s",
                     appid, sid, (unsigned)ts, (unsigned)nonce, sig_enc);
        if (n <= 0 || (size_t)n >= sizeof(uri)) {
            ESP_LOGE(TAG, "ASR URI 过长");
            i2s_channel_disable(s_i2s_rx);
            return VK_PORT_FAIL;
        }

        esp_websocket_client_config_t ws_cfg = {
            .uri = uri,
            .use_global_ca_store = true,
            .disable_auto_reconnect = true,
            .task_stack = 6144,
            .task_prio = 5,
        };

        /* 证书：用 ESP-IDF 内置的 crt_bundle（约 130 个根 CA），
         * 不用自己下载腾讯云证书。在 port_esp32_init 里 attach 一次。 */
        if (esp_crt_bundle_attach(NULL) != ESP_OK) {
            ESP_LOGW(TAG, "crt_bundle attach 失败，WebSocket 可能连不上");
        }

        s_ws = esp_websocket_client_init(&ws_cfg);
        if (s_ws == NULL) {
            ESP_LOGE(TAG, "websocket init 失败");
            i2s_channel_disable(s_i2s_rx);
            return VK_PORT_FAIL;
        }

        esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);

        if (esp_websocket_client_start(s_ws) != ESP_OK) {
            ESP_LOGE(TAG, "websocket start 失败");
            esp_websocket_client_destroy(s_ws);
            s_ws = NULL;
            i2s_channel_disable(s_i2s_rx);
            return VK_PORT_FAIL;
        }
    }

    s_asr_running = 1;
    ESP_LOGI(TAG, "ASR 会话开始（mode=%u，provider=%s）", (unsigned)mode, provider);
    return VK_PORT_OK;
}

/** 停止采集并结束会话。 */
static void p_asr_stop(void)
{
    if (s_asr_running == 0) {
        return;
    }
    s_asr_running = 0;

    if (s_mic_ready != 0) {
        i2s_channel_disable(s_i2s_rx);
    }

    if (s_asr_mode == VK_ASR_MODE_STREAM) {
        /* 发结束帧：腾讯云要求发一个空的 binary 帧或带 "end" 的文本帧 */
        if (s_ws != NULL) {
            const char end_msg[] = "{\"type\":\"end\"}";
            esp_websocket_client_send_text(s_ws, end_msg, (int)strlen(end_msg),
                                           pdMS_TO_TICKS(1000));
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_websocket_client_stop(s_ws);
            esp_websocket_client_destroy(s_ws);
            s_ws = NULL;
        }
    } else {
        /* 整包：把缓存的 PCM 一次性 POST 到一句话识别接口。
         * 缓存的实现见 vk_asr_pump（环形缓冲），此处只标记 TODO。
         * TODO(port): 把缓存的 PCM 用 esp_http_client POST 到
         * https://asr.cloud.tencent.com/flash/v1/{appid}，带同样的简化签名。 */
    }

    ESP_LOGI(TAG, "ASR 会话结束（mode=%u）", (unsigned)s_asr_mode);
}

/** 供 app 层周期调用：把采集到的音频推给腾讯云。
 * 流式模式边采边发；整包模式先存环形缓冲，结束时一次性发。 */
void vk_asr_pump(void)
{
    static uint8_t buf[VK_MIC_READ_BYTES];

    if (s_asr_running == 0) {
        return;
    }

    size_t n = p_mic_read(buf, sizeof(buf), 0);
    if (n == 0u) {
        return;
    }

    if (s_asr_mode == VK_ASR_MODE_STREAM) {
        /* 边采边发：腾讯云实时语音识别接受 1 秒以内的 binary 帧 */
        if (s_ws != NULL) {
            esp_websocket_client_send_bin(s_ws, (const char *)buf, (int)n,
                                          pdMS_TO_TICKS(200));
        }
    } else {
        /* TODO(port): 整包模式 -> 写入环形缓冲（PSRAM） */
    }
}

/* ============================ 接口表 ========================== */
const vk_port_t port_esp32 = {
    p_now_ms,
    p_keys_sample,
    p_led_set,
    p_send_frame,
    p_net_poll,
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

/* ============================ 初始化 ========================== */
static void init_gpio(void)
{
    gpio_config_t io;
    unsigned i;

    /* 10 个数字输入：内部上拉，按键低有效 */
    memset(&io, 0, sizeof(io));
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    for (i = 0u; i < VK_RAW_INPUT_COUNT; i++) {
        io.pin_bit_mask |= (1ULL << s_key_gpio[i]);
    }
    ESP_ERROR_CHECK(gpio_config(&io));

    /* 两颗 LED：推挽输出，默认熄灭 */
    memset(&io, 0, sizeof(io));
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    io.pin_bit_mask = (1ULL << VK_BOARD_LED_BLUE_GPIO)
                    | (1ULL << VK_BOARD_LED_RED_GPIO);
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level((gpio_num_t)VK_BOARD_LED_BLUE_GPIO, 0);
    gpio_set_level((gpio_num_t)VK_BOARD_LED_RED_GPIO, 0);
}

static void init_uart1(void)
{
    uart_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.baud_rate = (int)VK_BOARD_UART1_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity    = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_param_config((uart_port_t)1, &cfg));
    ESP_ERROR_CHECK(uart_set_pin((uart_port_t)1,
                                 (int)VK_BOARD_UART1_TX_GPIO,
                                 (int)VK_BOARD_UART1_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install((uart_port_t)1, 1024, 0, 0, NULL, 0));
}

void port_esp32_get_mac4(char out[8])
{
    uint8_t mac[6] = {0};
    if (out == NULL) {
        return;
    }
    /* TODO(port): esp_read_mac(mac, ESP_MAC_WIFI_STA) 后取后 4 位格式化。
     * 当前返回占位值，避免 main.c 因空指针出错。 */
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    (void)snprintf(out, 8, "%02X%02X", (unsigned)mac[4], (unsigned)mac[5]);
}

int port_esp32_init(const port_esp32_hooks_t *hooks)
{
    if (hooks != NULL) {
        s_hooks = *hooks;
    } else {
        memset(&s_hooks, 0, sizeof(s_hooks));
    }

    init_gpio();
    init_uart1();

    if (nvs_open("vibekey", NVS_READWRITE, &s_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open 失败");
        return -1;
    }

    /* USB CDC 单独建任务跑 TinyUSB 协议栈；收到的字节由该任务泵给应用层 */
    p_usb_init(hooks);

    /* ---- WiFi ---- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* AP 与 STA 两个 netif 都建出来。AP/STA 切换只改 WiFi 模式，
     * 不反复 create/destroy netif——反复建会造成内存泄漏。 */
    s_netif_ap = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();
    if ((s_netif_ap == NULL) || (s_netif_sta == NULL)) {
        ESP_LOGE(TAG, "wifi netif 创建失败");
        return -1;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t we = esp_wifi_init(&wifi_cfg);
    if (we != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败: %s", esp_err_to_name(we));
        return -1;
    }
    /* 先设成 NULL 模式：真正用 AP 还是 STA 由 vk_net_fsm 决定 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    /* 网络字节队列：网络任务放，net_poll 在应用层上下文 drain */
    s_net_queue = xQueueCreate(VK_NET_QUEUE_DEPTH, sizeof(net_chunk_t));
    if (s_net_queue == NULL) {
        ESP_LOGE(TAG, "网络队列创建失败");
        return -1;
    }

    /* 两个服务任务常驻，AP/STA 切换不重启它们 */
    xTaskCreate(vk_tcp_task, "vk_tcp", 4096, NULL, 5, NULL);
    xTaskCreate(vk_udp_task, "vk_udp", 3072, NULL, 5, NULL);
    xTaskCreate(vk_cloud_task, "vk_cloud", 6144, NULL, 5, NULL);

    /* 唤醒日志也走 UART1，便于开发期观察移植层是否就绪 */
    p_uart1_print("STATUS:PORT_READY");

    ESP_LOGI(TAG, "移植层就绪");
    return 0;
}
