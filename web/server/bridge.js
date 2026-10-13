/**
 * WebSocket 桥——设备（WiFi 主动连云）与浏览器（控制台）配对转发。
 *
 * 通道：
 *   设备  -> /ws/device?token=<device_token>
 *   浏览器 -> /ws/client?token=<user_jwt>&device=<device_token>
 *
 * 后端按 device_token 配对，双向转发二进制帧。
 *
 * 安全（设计文档 §7.1）：
 *   - 烧录指令（ENTER_DFU, 0x13）只允许 USB 通道。WiFi 桥在转发前丢弃这类帧，
 *     固件侧也会拒绝（cmd_dispatch 的通道校验），双保险。
 *   - 设备 token 由用户在前端生成，配网时写入设备；浏览器必须用 JWT 登录
 *     且设备属于该用户才能连。
 */
import { WebSocketServer, WebSocket } from 'ws'
import { verifyToken } from './auth.js'
import { getDeviceByToken, touchDevice } from './db.js'

const T_ENTER_DFU = 0x13
const SOF = 0xAA
const HEADER_LEN = 6
const CRC_LEN = 2

/** 从一段字节里找出所有 ENTER_DFU 帧的起止偏移（粗略扫描，用于丢弃）。 */
function* dfuFrameRanges(buf) {
  let i = 0
  while (i + MIN_FRAME_LEN <= buf.length) {
    if (buf[i] !== SOF) { i++; continue }
    const len = buf[i + 1] | (buf[i + 2] << 8)
    const total = HEADER_LEN + len + CRC_LEN
    if (i + total > buf.length) break
    if (buf[i + 3] === T_ENTER_DFU) {
      yield [i, i + total]
    }
    i += total
  }
}

const MIN_FRAME_LEN = HEADER_LEN + CRC_LEN

/** 丢弃字节流里的 ENTER_DFU 帧，返回过滤后的新 buffer。 */
export function stripDfuFrames(buf) {
  const ranges = [...dfuFrameRanges(buf)]
  if (ranges.length === 0) return buf
  const parts = []
  let last = 0
  for (const [s, e] of ranges) {
    if (s > last) parts.push(buf.slice(last, s))
    last = e
  }
  if (last < buf.length) parts.push(buf.slice(last))
  return Buffer.concat(parts)
}

/** 设备连接池：device_token -> { ws, userId, ip, clients:Set<ws> } */
const devices = new Map()

export function setupBridge(server) {
  const wss = new WebSocketServer({ noServer: true })

  server.on('upgrade', (req, socket, head) => {
    const url = new URL(req.url, 'http://localhost')
    const path = url.pathname

    if (path === '/ws/device') {
      handleDeviceUpgrade(req, socket, head, url)
    } else if (path === '/ws/client') {
      handleClientUpgrade(req, socket, head, url, wss)
    } else {
      socket.destroy()
    }
  })

  return wss
}

function handleDeviceUpgrade(req, socket, head, url) {
  const token = url.searchParams.get('token')
  if (!token) {
    socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n')
    socket.destroy()
    return
  }
  const device = getDeviceByToken(token)
  if (!device) {
    socket.write('HTTP/1.1 403 Forbidden\r\n\r\n')
    socket.destroy()
    return
  }

  const wss = new WebSocketServer({ noServer: true })
  wss.handleUpgrade(req, socket, head, (ws) => {
    const ip = req.socket.remoteAddress
    console.log(`[bridge] 设备上线 token=${token.slice(0, 8)}… ip=${ip}`)

    const entry = devices.get(token) || { ws: null, userId: device.user_id, ip, clients: new Set() }
    entry.ws = ws
    entry.ip = ip
    entry.userId = device.user_id
    devices.set(token, entry)
    touchDevice(token, ip)

    // 通知所有连着的浏览器：设备上线了
    broadcastToClients(entry, JSON.stringify({ type: 'device_online', device: token }))

    ws.on('message', (data, isBinary) => {
      if (!isBinary) return
      // 设备 -> 浏览器：原样转发（按键字符、状态、ASR 文本）
      for (const client of entry.clients) {
        if (client.readyState === WebSocket.OPEN) {
          client.send(data, { binary: true })
        }
      }
    })

    ws.on('close', () => {
      console.log(`[bridge] 设备离线 token=${token.slice(0, 8)}…`)
      entry.ws = null
      broadcastToClients(entry, JSON.stringify({ type: 'device_offline', device: token }))
    })

    ws.on('error', () => { /* 由 close 处理 */ })
  })
}

function handleClientUpgrade(req, socket, head, url, wss) {
  const jwt = url.searchParams.get('token')
  const deviceToken = url.searchParams.get('device')

  const payload = verifyToken(jwt)
  if (!payload) {
    socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n')
    socket.destroy()
    return
  }
  if (!deviceToken) {
    socket.write('HTTP/1.1 400 Bad Request\r\n\r\n')
    socket.destroy()
    return
  }
  const device = getDeviceByToken(deviceToken)
  if (!device || device.user_id !== payload.userId) {
    socket.write('HTTP/1.1 403 Forbidden\r\n\r\n')
    socket.destroy()
    return
  }

  wss.handleUpgrade(req, socket, head, (ws) => {
    const entry = devices.get(deviceToken) || { ws: null, userId: device.user_id, ip: null, clients: new Set() }
    entry.clients.add(ws)
    devices.set(deviceToken, entry)

    console.log(`[bridge] 浏览器连接 user=${payload.username} device=${deviceToken.slice(0, 8)}… 在线=${entry.ws !== null}`)

    // 告诉浏览器当前设备在线状态
    ws.send(JSON.stringify({
      type: entry.ws ? 'device_online' : 'device_offline',
      device: deviceToken,
    }))

    ws.on('message', (data, isBinary) => {
      if (!isBinary) return
      // 浏览器 -> 设备：先丢弃烧录帧，再转发
      const filtered = stripDfuFrames(Buffer.from(data))
      if (filtered.length === 0) {
        ws.send(JSON.stringify({ type: 'warn', message: '烧录指令只允许 USB 通道，已从 WiFi 桥丢弃' }))
        return
      }
      if (entry.ws && entry.ws.readyState === WebSocket.OPEN) {
        entry.ws.send(filtered, { binary: true })
      } else {
        ws.send(JSON.stringify({ type: 'warn', message: '设备当前离线，消息未送达' }))
      }
    })

    ws.on('close', () => {
      entry.clients.delete(ws)
    })

    ws.on('error', () => {
      entry.clients.delete(ws)
    })
  })
}

function broadcastToClients(entry, text) {
  for (const client of entry.clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(text)
    }
  }
}

/** 供 REST API 查询设备在线状态。 */
export function isDeviceOnline(deviceToken) {
  const entry = devices.get(deviceToken)
  return entry?.ws?.readyState === WebSocket.OPEN
}
