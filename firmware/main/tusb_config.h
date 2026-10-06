/**
 * @file tusb_config.h
 * @brief TinyUSB 设备配置 —— 只开 CDC（虚拟串口）。
 *
 * 冻结依据：docs/00-技术设计文档.md §5.5、§6.1、§7.1
 *
 * 为什么必须自己提供这个文件：TinyUSB 的所有可配置项都集中在 tusb_config.h 里，
 * 组件本身不带默认配置（tusb_option.h 会直接 #include "tusb_config.h"，
 * 找不到就编译失败）。
 *
 * 关键取舍：只启用 CDC 一个类。虽然组件的 CMakeLists 里固定编译所有设备类的
 * .c 文件，但只要这里把对应开关设为 0，那些文件就会退化成空壳，不占 flash。
 *
 * VID/PID 固定为 303A/4001（Espressif 默认 CDC 值）。上位机按这对值枚举设备；
 * 下载模式下 ROM 的 USB Serial/JTAG 是 303A/1001，两者不同，
 * 这正是上位机要处理"COM 口跳变"的原因。
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

/*--------------------------------------------------------------------+
 * 板级配置
 *--------------------------------------------------------------------*/

/* ESP32-S3 只有一组 USB-OTG，固定用 port 0 */
#define BOARD_TUD_RHPORT          0

/* 全速（12 Mbps）。键盘的心跳/按键字符/ASR 文本用全速绰绰有余 */
#define BOARD_TUD_MAX_SPEED       OPT_MODE_DEFAULT_SPEED

/*--------------------------------------------------------------------+
 * 通用配置
 *--------------------------------------------------------------------*/

/* CFG_TUSB_MCU 与 CFG_TUSB_RHPORT0_MODE 由编译参数传入（见 main/CMakeLists.txt），
 * 这里不重复定义，避免与组件传进来的值冲突。 */

#ifndef CFG_TUSB_OS
/* 用 FreeRTOS：TinyUSB 的 CDC 读写在独立任务里做，不会阻塞主循环 */
#define CFG_TUSB_OS               OPT_OS_FREERTOS
#endif

/* 调试等级。开发期可临时调到 2 看 USB 枚举过程；正常出包保持 0 */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            0
#endif

/* 启用设备协议栈 */
#define CFG_TUD_ENABLED           1

#define CFG_TUD_MAX_SPEED         BOARD_TUD_MAX_SPEED

/* ESP32-S3 的 DWC2 控制器要求 DMA 缓冲区 4 字节对齐 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))
#endif

/*--------------------------------------------------------------------+
 * 设备配置
 *--------------------------------------------------------------------*/

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

/*------------- 类开关：只留 CDC -------------*/
#define CFG_TUD_CDC               1
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_MIDI_2_0          0
#define CFG_TUD_VENDOR            0
#define CFG_TUD_DFU_RT            0
#define CFG_TUD_ECM_RNDIS         0
#define CFG_TUD_NCM               0
#define CFG_TUD_BTH               0
#define CFG_TUD_AUDIO             0
#define CFG_TUD_VIDEO             0
#define CFG_TUD_MTP               0
#define CFG_TUD_PRINTER           0
#define CFG_TUD_USBTMC            0

/* 用 CDC 的通知端点（串口状态变化，如 DTR/RTS/CD）*/
#define CFG_TUD_CDC_NOTIFY        1

/* CDC FIFO 大小。全速下 64 字节一包，缓冲取 512 以吸收突发：
   连按多个按键或 ASR 长文本时不会因为主机没及时读而丢数据。 */
#define CFG_TUD_CDC_RX_BUFSIZE    512
#define CFG_TUD_CDC_TX_BUFSIZE    512

#define CFG_TUD_CDC_RX_EPSIZE     64
#define CFG_TUD_CDC_TX_EPSIZE     64

#ifdef __cplusplus
 }
#endif

#endif /* TUSB_CONFIG_H_ */
