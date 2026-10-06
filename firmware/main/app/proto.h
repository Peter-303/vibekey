/**
 * @file proto.h
 * @brief VibeKey 通信协议编解码 —— USB CDC 与 WiFi TCP 共用同一套帧格式。
 *
 * 冻结依据：docs/00-技术设计文档.md §3
 *
 * 本文件与 host/src/VibeKey.App/Services/FrameCodec.cs 必须逐字节一致，
 * 两侧使用同一组测试向量（见 test/test_proto.c 与 FrameCodecTests）。
 * 任何字段变更必须同时改三处：本文档、proto.c、FrameCodec.cs。
 *
 * 帧格式：
 *   +------+---------+------+---------+------------+---------+
 *   | 0xAA | LEN(2B) | TYPE | SEQ(2B) | PAYLOAD(N) | CRC16   |
 *   |  1   |  LE     |  1   |  LE     |  LEN bytes |  LE(2B) |
 *   +------+---------+------+---------+------------+---------+
 *     0        1        3       4          6       6+LEN
 *
 * CRC16/CCITT-FALSE：poly 0x1021, init 0xFFFF, 不反射, xorout 0x0000。
 * CRC 覆盖 LEN+TYPE+SEQ+PAYLOAD（不含 SOF，不含 CRC 自身）。
 */
#ifndef VIBEKEY_PROTO_H
#define VIBEKEY_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------- 冻结参数 ------------------------------- */
#define PROTO_SOF           0xAAu   /**< 帧头 */
#define PROTO_MAX_PAYLOAD   1024u   /**< 最大载荷字节数 */
#define PROTO_HEADER_LEN    6u      /**< SOF+LEN+TYPE+SEQ */
#define PROTO_CRC_LEN       2u
#define PROTO_MIN_FRAME_LEN 8u      /**< LEN=0 时的最小帧长 */
#define PROTO_VERSION       1u
#define PROTO_DFU_MAGIC_LEN 8u

/* -------------------------------- TYPE --------------------------------- */
#define PROTO_T_KEY_EVENT   0x01u   /**< 上行 按键/旋钮事件，载荷 1 字节字符 */
#define PROTO_T_STATUS      0x02u   /**< 上行 状态上报，载荷状态字符串 */
#define PROTO_T_ASR_TEXT    0x03u   /**< 上行 ASR 文本，首字节 0=中间 1=最终 */
#define PROTO_T_PONG        0x04u   /**< 上行 心跳应答，载荷空 */
#define PROTO_T_KEY_RELEASE 0x05u   /**< 上行 按键释放，载荷 1 字节字符 */
#define PROTO_T_ACK         0x06u   /**< 上行 指令应答，首字节 0=ACK 1=NACK */
#define PROTO_T_PING        0x10u   /**< 下行 心跳请求，载荷空 */
#define PROTO_T_WIFI_CFG    0x11u   /**< 下行 WiFi 配置 "SSID\0PASS\0" */
#define PROTO_T_ASR_CFG     0x12u   /**< 下行 ASR 配置 "K=V\0K=V\0...\0" */
#define PROTO_T_ENTER_DFU   0x13u   /**< 下行 进入烧录，载荷 8 字节 magic */
#define PROTO_T_VOICE_CTL   0x14u   /**< 下行 语音启停，载荷 1 字节 */
#define PROTO_T_STATUS_REQ  0x15u   /**< 下行 请求立即上报状态 */
#define PROTO_T_VER         0x7Fu   /**< 双向 协议版本协商 "VER=1" */

/* ASR 文本首字节 */
#define PROTO_ASR_PARTIAL   0x00u
#define PROTO_ASR_FINAL     0x01u

/* ACK 首字节 */
#define PROTO_ACK_OK        0x00u
#define PROTO_ACK_NACK      0x01u

/* 语音控制载荷 */
#define PROTO_VOICE_STOP    0x00u
#define PROTO_VOICE_START   0x01u

/** 烧录指令 magic word = "VIBEFLAS"（混淆用途，非安全机制，见设计文档 §7.1） */
extern const uint8_t proto_dfu_magic[PROTO_DFU_MAGIC_LEN];

/* ------------------------------ 通道标识 ------------------------------- */
/* 放在 proto.h 而不是 port.h：通道是协议/传输层概念，与硬件无关 */
#define VK_CH_USB    0x01u   /**< USB CDC 虚拟串口（主通道） */
#define VK_CH_WIFI   0x02u   /**< WiFi TCP（辅通道） */
#define VK_CH_UART1  0x04u   /**< 仅调试，不承载业务帧 */

/* ------------------------------- 帧结构 --------------------------------- */
typedef struct {
    uint8_t  type;
    uint16_t seq;
    uint16_t len;
    uint8_t  payload[PROTO_MAX_PAYLOAD];
} proto_frame_t;

/** 解码结果。consumed 语义见 proto_decode() 注释。 */
typedef enum {
    PROTO_DEC_OK = 0,      /**< 解出一帧，consumed = 整帧长度 */
    PROTO_DEC_NEED_MORE,   /**< 数据不足或缓冲区无帧头，consumed = 可安全丢弃的字节数 */
    PROTO_DEC_BAD_CRC,     /**< CRC 不符，consumed = 1（跳过 SOF 重同步） */
    PROTO_DEC_BAD_LEN,     /**< LEN 越界，consumed = 1 */
    PROTO_DEC_BAD_ARG      /**< 参数非法 */
} proto_dec_result_t;

/* -------------------------------- API ---------------------------------- */

/** CRC16/CCITT-FALSE。data 为 NULL 时返回 0。 */
uint16_t proto_crc16(const uint8_t *data, size_t len);

/** 该 TYPE 是否为下行（上位机 -> 设备）。 */
int proto_type_is_downlink(uint8_t type);

/**
 * 编码一帧。
 * @return 0 成功；-1 参数非法；-2 len 越界；-3 输出缓冲不足
 */
int proto_encode(const proto_frame_t *f, uint8_t *out, size_t out_cap, size_t *out_len);

/** 一帧编码后的总长度。 */
size_t proto_frame_size(uint16_t payload_len);

/**
 * 从一段字节流中解出一帧。
 *
 * @param buf      待解析字节流
 * @param avail    buf 有效长度
 * @param out      解出的帧（成功时有效）
 * @param consumed 出参：调用方应当前进的字节数
 *                 - PROTO_DEC_OK        -> 整帧长度
 *                 - PROTO_DEC_NEED_MORE -> 可安全丢弃的垃圾字节数（无帧头时等于 avail）
 *                 - PROTO_DEC_BAD_CRC   -> 1
 *                 - PROTO_DEC_BAD_LEN   -> 1
 * @return proto_dec_result_t
 */
proto_dec_result_t proto_decode(const uint8_t *buf, size_t avail,
                                proto_frame_t *out, size_t *consumed);

/* ---------------------------- 流式解码器 -------------------------------- */
/**
 * 面向串口/WiFi 字节流的增量解码器。内部维护环形累积缓冲，
 * 调用方只需把收到的字节喂进来，一次可解出多帧。
 */
typedef struct {
    uint8_t  buf[PROTO_HEADER_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN];
    size_t   fill;
} proto_stream_t;

void proto_stream_init(proto_stream_t *s);

/**
 * 喂入一段字节。
 * @param out       输出帧数组
 * @param inout_cap 入参：out 容量；出参：实际解出的帧数
 * @return 最后一步解码的结果；解出至少一帧时返回 PROTO_DEC_OK
 */
proto_dec_result_t proto_stream_feed(proto_stream_t *s, const uint8_t *data, size_t len,
                                     proto_frame_t *out, size_t *inout_cap);

#ifdef __cplusplus
}
#endif

#endif /* VIBEKEY_PROTO_H */
