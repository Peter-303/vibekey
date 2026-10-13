/**
 * 云通道——浏览器经后端 WebSocket 桥控制 WiFi 设备。
 *
 * 与 SerialDevice（USB）同一套接口（onFrame / onStateChange / onLog / send），
 * 只是传输层从 Web Serial 换成 WebSocket。
 *
 * 连接：ws(s)://<host>/ws/client?token=<jwt>&device=<device_token>
 * 帧协议与固件逐字节一致（见 lib/frame.js）。
 */
import { FrameStream, encode, T, buildWifiConfig, DFU_MAGIC } from './frame.js'

export class CloudDevice {
  constructor() {
    this.ws = null
    this.stream = new FrameStream()
    this.seq = 0
    this.deviceToken = null
    this.jwt = null

    this.onFrame = null
    this.onStateChange = null
    this.onLog = null
  }

  get connected() {
    return this.ws !== null && this.ws.readyState === WebSocket.OPEN
  }

  log(text, isError = false) {
    this.onLog?.(text, isError)
  }

  setState(state, detail) {
    this.onStateChange?.(state, detail)
  }

  /**
   * 连到后端桥。
   * @param {string} wsUrl  如 ws://localhost:8080 或 wss://vibekey.example.com
   * @param {string} jwt    用户登录后的 JWT
   * @param {string} deviceToken  设备 token（后端生成，配网时写入设备）
   */
  connect(wsUrl, jwt, deviceToken) {
    if (this.connected) return Promise.resolve(true)

    this.jwt = jwt
    this.deviceToken = deviceToken

    const url = `${wsUrl.replace(/\/$/, '')}/ws/client?token=${encodeURIComponent(jwt)}&device=${encodeURIComponent(deviceToken)}`
    this.setState('connecting')
    this.log(`连云：${url.split('?')[0]}…`)

    return new Promise((resolve) => {
      let settled = false
      const ws = new WebSocket(url)
      ws.binaryType = 'arraybuffer'
      this.ws = ws

      ws.onopen = () => {
        settled = true
        this.setState('connected')
        this.log('已连上云桥')
        resolve(true)
      }

      ws.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) {
          const frames = this.stream.feed(new Uint8Array(ev.data))
          for (const f of frames) this.onFrame?.(f)
        } else {
          // 文本消息：设备上线/离线通知、警告
          try {
            const msg = JSON.parse(ev.data)
            if (msg.type === 'device_online') {
              this.log('设备已上线')
              this.setState('connected')
            } else if (msg.type === 'device_offline') {
              this.log('设备已离线', true)
              this.setState('disconnected', '设备离线')
            } else if (msg.type === 'warn') {
              this.log(msg.message, true)
            }
          } catch { /* 非 JSON，忽略 */ }
        }
      }

      ws.onerror = () => {
        if (!settled) {
          settled = true
          this.setState('error', '连接失败')
          this.log('云桥连接失败', true)
          resolve(false)
        }
      }

      ws.onclose = () => {
        this.ws = null
        this.stream.reset()
        this.setState('disconnected')
        this.log('云桥已断开')
      }
    })
  }

  disconnect() {
    try {
      this.ws?.close()
    } catch { /* ignore */ }
    this.ws = null
    this.stream.reset()
    this.setState('disconnected')
  }

  async send(type, payload = new Uint8Array(0)) {
    if (!this.connected) {
      this.log('未连接，无法发送', true)
      return false
    }
    const frame = encode(type, ++this.seq, payload)
    try {
      this.ws.send(frame)
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

  async sendCloudConfig(cloudUrl, cloudToken) {
    // 通过 ASR 配置帧（0x12）的扩展键下发云配置（不改协议）
    const text = `provider=tencent\0appid=x\0secret_id=x\0secret_key=x\0cloud_url=${cloudUrl}\0cloud_token=${cloudToken}\0`
    return this.send(T.ASR_CFG, new TextEncoder().encode(text))
  }
}
