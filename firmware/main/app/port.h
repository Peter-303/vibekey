/**
 * @file port.h
 * @brief 移植层接口 —— 应用层唯一依赖的外部符号集合。
 *
 * 架构依据：docs/00-技术设计文档.md §2.1（ADR-1）
 *
 * 用户已明确"移植层后面会有变动"。因此本文件是整个固件里
 * **唯一**与硬件/ESP-IDF 相关的接触面：
 *
 *   - app/ 下所有 .c 文件不得 #include 任何 ESP-IDF 头文件；
 *   - 所有外部能力都通过 const vk_port_t * 函数指针表获取；
 *   - 换硬件 / 换引脚 / 换 USB 方案 = 新增一个 port_xxx.c + 改 port_board.h，
 *     app/ 一行不改。
 *
 * 注入方式：main.c 启动时执行 `vk_port = &port_esp32;`，
 * 主机端测试由 test/stub_port.c 提供假实现。
 */
#ifndef VIBEKEY_PORT_H
#define VIBEKEY_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "key_fsm.h"   /* VK_RAW_INPUT_COUNT */
#include "proto.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 通道标识已移到 proto.h（通道是协议层概念，与硬件无关） */

/* 语音模式 */
#define VK_ASR_MODE_STREAM  0u   /**< 流式：按住说话，边说边出字 */
#define VK_ASR_MODE_ONESHOT 1u   /**< 整包：点一下录，再点一下停 */

/* ASR 凭据缓冲上限。port 层读 NVS、app 层解析 0x12 帧都要用，放在 port.h
 * 这个双方共同的头里，避免 port 层 include app 层的头。 */
#define VK_ASR_PROVIDER_MAX  16
#define VK_ASR_APPID_MAX     32
#define VK_ASR_SECRET_MAX    64
#define VK_ASR_FORMAT_MAX    8

/* NVS 返回值 */
#define VK_PORT_OK    0
#define VK_PORT_FAIL  (-1)
#define VK_PORT_NOENT  (-2)   /**< 键不存在 */

/**
 * 移植层接口表。
 *
 * 约定：
 *   - 所有函数在应用层上下文（任务）中被调用，不得无限阻塞；
 *   - send_frame 必须把同一个 seq 的帧同时发往 USB 与 WiFi 两条通道
 *     （见设计文档 §3.4 规则 2）；
 *   - uart1_print 只用于调试状态行，绝不打印密码 / API Key；
 *   - 返回 int 的函数统一使用 VK_PORT_* 错误码。
 */
typedef struct vk_port {
    /* ---------------------------- 时间 ---------------------------- */
    /** 单调毫秒计数，允许回绕。 */
    uint32_t (*now_ms)(void);

    /* --------------------------- 输入 ----------------------------- */
    /** 采一整拍原始电平。raw[0..6]=K1..K7, raw[7]=encA, raw[8]=encB, raw[9]=encSW；1=按下 */
    void (*keys_sample)(uint8_t raw[VK_RAW_INPUT_COUNT]);

    /* --------------------------- 输出 ----------------------------- */
    /** 直接控制两颗 LED 的电平。由 app 每拍调用，实现方可做 PWM 但不得改变语义。 */
    void (*led_set)(uint8_t blue_on, uint8_t red_on);

    /**
     * 发送一个上行帧到 USB + WiFi 双通道，使用调用方给定的 seq。
     * 实现方负责把 data 编码后的字节写出；data 为已完成编码的整帧字节。
     */
    void (*send_frame)(const uint8_t *encoded, size_t len);

    /** UART1 调试行（不含 \r\n，由实现方追加）。115200 8N1。 */
    void (*uart1_print)(const char *line);

    /* --------------------------- 系统 ----------------------------- */
    /** 重启设备。实现方需保证返回前 NVS 已提交、TWDT 已妥善处理。 */
    void (*reboot)(void);

    /** 喂看门狗。 */
    void (*wdt_feed)(void);

    /** 任务级延时（毫秒）。用于让出 CPU，不得忙等。 */
    void (*sleep_ms)(uint32_t ms);

    /* ---------------------------- NVS ----------------------------- */
    int (*nvs_set_str)(const char *key, const char *val);
    int (*nvs_get_str)(const char *key, char *out, size_t cap);
    int (*nvs_commit)(void);
    int (*nvs_erase_all)(void);

    /* --------------------------- WiFi ----------------------------- */
    /** 启动 AP 配网模式。ssid 形如 VibeKey-1A2B。 */
    int (*wifi_start_ap)(const char *ssid, const char *pass);
    /** 尝试连接路由器。 */
    int (*wifi_start_sta)(const char *ssid, const char *pass);
    /** 断开并关闭 WiFi。 */
    void (*wifi_stop)(void);
    /** 当前 RSSI，0 表示无效 / 未连接。 */
    int (*wifi_rssi)(void);
    /** 当前 IP 字符串，空表示未取得。 */
    int (*wifi_ip)(char *out, size_t cap);

    /* ---------------------------- 音频 ---------------------------- */
    /** 初始化麦克风。失败返回 VK_PORT_FAIL（对应 ERROR,ERR=MIC_FAIL）。 */
    int (*mic_init)(void);
    /** 开始采集并推流给 ASR。mode = VK_ASR_MODE_* */
    int (*asr_start)(uint8_t mode);
    /** 停止采集 / 结束会话。 */
    void (*asr_stop)(void);
} vk_port_t;

/**
 * 移植层实例。由 main.c 注入，应用层只读。
 * 未绑定时为 NULL，应用层各模块应判空后安全返回。
 */
extern const vk_port_t *vk_port;

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_PORT_H */
