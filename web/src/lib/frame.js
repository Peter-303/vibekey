/**
 * 帧编解码器——与固件 firmware/main/app/proto.c 逐字节一致。
 *
 * 帧格式（设计文档 §3 已冻结）：
 *   0xAA | LEN(2B,LE) | TYPE(1B) | SEQ(2B,LE) | PAYLOAD(N) | CRC16(2B,LE)
 * CRC16/CCITT-FALSE：poly 0x1021, init 0xFFFF, 不反射, xorout 0x0000。
 */

export const SOF = 0xAA
export const MAX_PAYLOAD = 1024
export const HEADER_LEN = 6
export const CRC_LEN = 2
export const MIN_FRAME_LEN = 8

export const T = {
  KEY_EVENT: 0x01,
  STATUS: 0x02,
  ASR_TEXT: 0x03,
  PONG: 0x04,
  KEY_RELEASE: 0x05,
  ACK: 0x06,
  PING: 0x10,
  WIFI_CFG: 0x11,
  ASR_CFG: 0x12,
  ENTER_DFU: 0x13,
  VOICE_CTL: 0x14,
  STATUS_REQ: 0x15,
  VER: 0x7F,
}

export const DFU_MAGIC = new Uint8Array([0x56, 0x49, 0x42, 0x45, 0x46, 0x4C, 0x41, 0x53]) // "VIBEFLAS"

export function crc16(data) {
  let crc = 0xFFFF
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i] << 8
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF
    }
  }
  return crc
}

export function frameSize(payloadLen) {
  return HEADER_LEN + payloadLen + CRC_LEN
}

/** 编码一帧，返回 Uint8Array。 */
export function encode(type, seq, payload = new Uint8Array(0)) {
  if (payload.length > MAX_PAYLOAD) {
    throw new Error(`载荷长度 ${payload.length} 超过上限 ${MAX_PAYLOAD}`)
  }
  const total = frameSize(payload.length)
  const out = new Uint8Array(total)
  out[0] = SOF
  out[1] = payload.length & 0xFF
  out[2] = (payload.length >> 8) & 0xFF
  out[3] = type
  out[4] = seq & 0xFF
  out[5] = (seq >> 8) & 0xFF
  out.set(payload, HEADER_LEN)
  const crc = crc16(out.subarray(1, HEADER_LEN + payload.length))
  out[total - 2] = crc & 0xFF
  out[total - 1] = (crc >> 8) & 0xFF
  return out
}

/** 从字节流解出一帧。返回 { result, frame, consumed }。 */
export function decode(buf, start = 0) {
  let i = start
  while (i < buf.length && buf[i] !== SOF) i++
  if (i >= buf.length) return { result: 'need_more', consumed: buf.length - start }
  if (i > start) return { result: 'need_more', consumed: i - start }
  if (buf.length - i < HEADER_LEN) return { result: 'need_more', consumed: 0 }

  const len = buf[i + 1] | (buf[i + 2] << 8)
  if (len > MAX_PAYLOAD) return { result: 'bad_len', consumed: 1 }

  const total = frameSize(len)
  if (buf.length - i < total) return { result: 'need_more', consumed: 0 }

  const want = buf[i + total - 2] | (buf[i + total - 1] << 8)
  const got = crc16(buf.subarray(i + 1, i + total - 2))
  if (want !== got) return { result: 'bad_crc', consumed: 1 }

  return {
    result: 'ok',
    frame: {
      type: buf[i + 3],
      seq: buf[i + 4] | (buf[i + 5] << 8),
      payload: buf.slice(i + HEADER_LEN, i + total - CRC_LEN),
    },
    consumed: total,
  }
}

/** 流式解码器：累积字节，按需解帧。 */
export class FrameStream {
  constructor() {
    this.buf = new Uint8Array(0)
  }

  feed(data) {
    const merged = new Uint8Array(this.buf.length + data.length)
    merged.set(this.buf, 0)
    merged.set(data, this.buf.length)
    this.buf = merged

    const frames = []
    let offset = 0
    for (;;) {
      const r = decode(this.buf.subarray(offset))
      if (r.result === 'ok') {
        frames.push(r.frame)
        offset += r.consumed
      } else if (r.result === 'bad_crc' || r.result === 'bad_len') {
        offset += r.consumed
      } else {
        offset += r.consumed
        break
      }
    }
    this.buf = this.buf.slice(offset)
    return frames
  }

  reset() {
    this.buf = new Uint8Array(0)
  }
}

/** 构造 WiFi 配置载荷 "SSID\0PASS\0"。 */
export function buildWifiConfig(ssid, password) {
  const s = new TextEncoder().encode(ssid)
  const p = new TextEncoder().encode(password)
  const out = new Uint8Array(s.length + 1 + p.length + 1)
  out.set(s, 0)
  out.set(p, s.length + 1)
  return out
}

/** 把载荷当 UTF-8 文本读（去掉结尾 NUL）。 */
export function payloadText(payload) {
  let end = payload.length
  if (end > 0 && payload[end - 1] === 0) end--
  return new TextDecoder().decode(payload.subarray(0, end))
}
