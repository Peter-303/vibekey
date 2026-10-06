/**
 * @file port_board.h
 * @brief ★ GPIO 引脚分配表 —— 硬件变动时**只需要改这一个文件**。
 *
 * 状态：占位宏，待硬件组提供最终引脚分配表后填入。
 *       当前值按 ESP32-S3-WROOM-1 模组的常见安全选择占位，
 *       **未经硬件验证，不得直接烧录**。
 *
 * 填写要求（见 docs/01-引脚分配表.md）：
 *   - 按键使用 GPIO 1..21 中未被 USB/JTAG/Flash 占用的引脚；
 *   - ESP32-S3 的 GPIO 26..32 用于 SPI Flash/PSRAM，禁用；
 *   - GPIO 19/20 若用于 USB-OTG（D-/D+），则不得再分配；
 *   - 编码器 A/B 建议分配到支持中断的任意 RTC/数字 GPIO；
 *   - 麦克风：I2S 需 BCK/WS/DIN 三根，PDM 需 CLK/DATA 两根，
 *     模拟麦需一路 ADC1 通道。三者引脚需求不同，
 *     **必须先确定音频硬件选型再填**（PRD 标为开放问题）。
 */

#ifndef VIBEKEY_PORT_BOARD_H
#define VIBEKEY_PORT_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* ======================= 以下为占位值，待硬件定稿 ======================= */

/* ---- 7 个机械按键（GPIO 编号，低电平有效由 port_esp32.c 内部处理） ---- */
#define VK_BOARD_KEY1_GPIO   1u
#define VK_BOARD_KEY2_GPIO   2u
#define VK_BOARD_KEY3_GPIO   3u
#define VK_BOARD_KEY4_GPIO   4u
#define VK_BOARD_KEY5_GPIO   5u
#define VK_BOARD_KEY6_GPIO   6u
#define VK_BOARD_KEY7_GPIO   7u

/* ---- 旋转编码器 ---- */
#define VK_BOARD_ENC_A_GPIO  8u   /**< A 相 */
#define VK_BOARD_ENC_B_GPIO  9u   /**< B 相 */
#define VK_BOARD_ENC_SW_GPIO 10u  /**< 编码器按下 */

/* ---- 状态 LED ---- */
#define VK_BOARD_LED_BLUE_GPIO 11u
#define VK_BOARD_LED_RED_GPIO  12u

/* ---- 麦克风 ----
 *
 * 硬件已定为 **TDK InvenSense ICS-43434**（BOM 第 41 项，U11）：
 * I2S 数字 MEMS 麦克风，6 引脚（WS/SCK/SD/L/R/GND/VDD），
 * 直接输出 16bit PCM，无需外挂 codec。
 *
 * 接口类型锁定为 I2S；引脚仍待硬件组确认，先占位。
 * 硬件变动时只改下面的 GPIO 宏，port_esp32.c 的 I2S 驱动不用动。 */
#define VK_BOARD_MIC_TYPE        VK_MIC_TYPE_I2S

#define VK_MIC_TYPE_TBD          0
#define VK_MIC_TYPE_I2S          1
#define VK_MIC_TYPE_PDM          2
#define VK_MIC_TYPE_ANALOG_ADC   3

/* I2S 引脚（ICS-43434 的三根信号线）。
 * ICS-43434 的 L/R 脚接 GND 表示左声道；我们的软件只读左声道（单声道）。
 * 占位值，待硬件组确认： */
#define VK_BOARD_I2S_BCK_GPIO    13u   /**< BCK / SCK：位时钟 */
#define VK_BOARD_I2S_WS_GPIO     14u   /**< WS / LRCK：左右声道选择（帧同步） */
#define VK_BOARD_I2S_DIN_GPIO    15u   /**< SD / DIN：数据输入（麦克风 SD 脚接这里） */

/* ICS-43434 的 L/R 选择脚：接 GND 为左声道，接 VDD 为右声道。
 * 如果硬件上是固定接死（常见做法），固件不用管；
 * 如果接到 GPIO 做成可切换，取消下面这行的注释并填真实 GPIO。 */
/* #define VK_BOARD_I2S_LRSEL_GPIO 18u */

/* ---- UART1 调试口（PRD 5.3 明确使用 UART1，非 USB-CDC、非 log 口） ---- */
#define VK_BOARD_UART1_TX_GPIO   16u
#define VK_BOARD_UART1_RX_GPIO   17u
#define VK_BOARD_UART1_BAUD      115200u

/* ---- USB 数据通道（TinyUSB CDC）----
 * ESP32-S3 的 USB-OTG 与 USB Serial/JTAG 共用同一对引脚 GPIO20(DP)/GPIO19(DM)，
 * 不存在"只接一个"的布线方式：USB-C 的 D+/D- 只要接到这两颗，两个外设都连在总线上，
 * 运行时由固件/ROM 决定谁占用。这两颗是芯片固定的，不可更改，此处仅作文档记录。
 * 烧录时的"COM 口跳变"见 docs/00-技术设计文档.md §7.1 情形 A。 */
#define VK_BOARD_USB_DP_GPIO     20u
#define VK_BOARD_USB_DM_GPIO     19u

/* ---- 电池电压检测（早期 PRD 2.1.1.4 提到 ADC 采集，本期可选） ---- */
#define VK_BOARD_BAT_ADC_GPIO    18u
#define VK_BOARD_BAT_ADC_ENABLED 0u   /**< 本期未启用 */

/* ======================= 占位值结束 ======================= */

/** 自检：编译期检查引脚是否越界（0..21 为 ESP32-S3 安全数字 IO 区）。 */
#define VK_BOARD_GPIO_VALID(p)   ((p) <= 21u)

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_PORT_BOARD_H */
