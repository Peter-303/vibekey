/**
 * Web Serial 连接管理——直连 VibeKey 的 USB CDC 虚拟串口。
 *
 * 浏览器要求：Chrome / Edge（Web Serial API）。Firefox/Safari 不支持。
 * 帧协议与固件逐字节一致（见 lib/frame.js）。
 */
import { FrameStream, encode, T, buildWifiConfig, DFU_MAGIC } from './frame.js'

export class SerialDevice {
  constructor() {
    this.port = null
    this.reader = null
    this.writer = null
    this.stream = new FrameStream()
    this.seq = 0
    this.reading = false

    // 事件
    this.onFrame = null        // (frame) => {}
    this.onStateChange = null  // (state: 'connected'|'connecting'|'disconnected'|'error', detail?) => {}
    this.onLog = null          // (text, isError?) => {}
  }

  get connected() {
    return this.port !== null && this.reading
  }

  get supported() {
    return 'serial' in navigator
  }

  log(text, isError = false) {
    this.onLog?.(text, isError)
  }

  setState(state, detail) {
    this.onStateChange?.(state, detail)
  }

  async connect() {
    if (!this.supported) {
      this.setState('error', '当前浏览器不支持 Web Serial API。请用 Chrome 或 Edge。')
      return false
    }
    if (this.connected) return true

    try {
      this.setState('connecting')
      this.log('请选择 VibeKey 的串口（VID 303A / PID 4001）…')
      this.port = await navigator.serial.requestPort()
      await this.port.open({ baudRate: 115200 })
      this.log('已连接')

      this.writer = this.port.writable.getWriter()
      this.reading = true
      this.setState('connected')
      this.readLoop().catch((err) => {
        this.log(`读取中断：${err.message}`, true)
        this.disconnect()
      })
      return true
    } catch (err) {
      this.setState('error', err.name === 'NotFoundError' ? '已取消选择' : err.message)
      this.log(`连接失败：${err.message}`, true)
      await this.disconnect()
      return false
    }
  }

  async disconnect() {
    this.reading = false
    try {
      this.reader?.cancel()
    } catch { /* ignore */ }
    try {
      this.reader?.releaseLock()
      this.writer?.releaseLock()
      await this.port?.close()
    } catch { /* ignore */ }
    this.reader = null
    this.writer = null
    this.port = null
    this.stream.reset()
    this.setState('disconnected')
    this.log('已断开')
  }

  async readLoop() {
    while (this.reading && this.port?.readable) {
      this.reader = this.port.readable.getReader()
      try {
        for (;;) {
          const { value, done } = await this.reader.read()
          if (done) break
          if (!value) continue
          const frames = this.stream.feed(value)
          for (const f of frames) this.onFrame?.(f)
        }
      } finally {
        try {
          this.reader.releaseLock()
        } catch { /* ignore */ }
      }
    }
  }

  async send(type, payload = new Uint8Array(0)) {
    if (!this.writer) {
      this.log('未连接，无法发送', true)
      return false
    }
    const frame = encode(type, ++this.seq, payload)
    try {
      await this.writer.write(frame)
      return true
    } catch (err) {
      this.log(`发送失败：${err.message}`, true)
      return false
    }
  }

  async ping() {
    return this.send(T.PING)
  }

  async requestStatus() {
    return this.send(T.STATUS_REQ)
  }

  async sendWifiConfig(ssid, password) {
    return this.send(T.WIFI_CFG, buildWifiConfig(ssid, password))
  }

  async enterDfu() {
    return this.send(T.ENTER_DFU, DFU_MAGIC)
  }
}

export function statusLabel(state) {
  switch (state) {
    case 'connected': return '已连接'
    case 'connecting': return '连接中…'
    case 'disconnected': return '未连接'
    case 'error': return '出错'
    default: return state
  }
}
