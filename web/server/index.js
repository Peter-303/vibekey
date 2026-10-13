/**
 * VibeKey 云端后端入口。
 *
 * 职责：
 *   - REST API：用户账号 / 设备管理 / 映射云端同步
 *   - WebSocket 桥：设备（WiFi 主动连云）↔ 浏览器（控制台）
 *   - 静态文件：serve 前端打包产物（web/dist）
 *
 * 启动：
 *   node index.js
 * 环境变量：
 *   PORT          默认 8080
 *   JWT_SECRET    生产必须改
 *   DB_PATH       SQLite 数据文件路径
 *   STATIC_DIR    前端打包产物目录，默认 ../dist
 */
import { createServer } from 'http'
import { readFileSync, existsSync, statSync } from 'fs'
import { extname, join, normalize, resolve } from 'path'
import { randomBytes } from 'crypto'
import { fileURLToPath } from 'url'
import { dirname } from 'path'

import { hashPassword, verifyPassword, signToken, requireAuth } from './auth.js'
import { createUser, getUserByName, registerDevice, getDevicesByUser, getMapping, saveMapping } from './db.js'
import { setupBridge, isDeviceOnline } from './bridge.js'

const __dirname = dirname(fileURLToPath(import.meta.url))
const PORT = parseInt(process.env.PORT || '8080', 10)
const STATIC_DIR = resolve(process.env.STATIC_DIR || join(__dirname, '..', 'dist'))

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.map': 'application/json',
}

function sendJson(res, status, obj) {
  const body = JSON.stringify(obj)
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Access-Control-Allow-Origin': '*',
  })
  res.end(body)
}

function readBody(req) {
  return new Promise((resolveBody, reject) => {
    const chunks = []
    req.on('data', (c) => chunks.push(c))
    req.on('end', () => {
      try {
        resolveBody(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'))
      } catch (err) {
        reject(err)
      }
    })
    req.on('error', reject)
  })
}

function serveStatic(req, res) {
  let path = decodeURIComponent(new URL(req.url, 'http://localhost').pathname)
  if (path === '/') path = '/index.html'
  // 防目录穿越
  const full = normalize(join(STATIC_DIR, path))
  if (!full.startsWith(STATIC_DIR)) {
    res.writeHead(403)
    res.end('Forbidden')
    return
  }
  const target = existsSync(full) && statSync(full).isFile()
    ? full
    : join(STATIC_DIR, 'index.html')   // SPA 回退
  if (!existsSync(target)) {
    res.writeHead(404)
    res.end('Not Found')
    return
  }
  const ext = extname(target)
  res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' })
  res.end(readFileSync(target))
}

async function handleApi(req, res) {
  const url = new URL(req.url, 'http://localhost')
  const path = url.pathname
  const method = req.method

  // CORS 预检
  if (method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type,Authorization',
    })
    res.end()
    return true
  }

  // ---- 认证 ----
  if (path === '/api/auth/register' && method === 'POST') {
    const { username, password } = await readBody(req)
    if (!username || !password || username.length < 2 || password.length < 6) {
      sendJson(res, 400, { error: '用户名至少 2 位，密码至少 6 位' })
      return true
    }
    if (getUserByName(username)) {
      sendJson(res, 409, { error: '用户名已被占用' })
      return true
    }
    const user = createUser(username, hashPassword(password))
    sendJson(res, 200, { token: signToken(user.id, username), user: { id: user.id, username } })
    return true
  }

  if (path === '/api/auth/login' && method === 'POST') {
    const { username, password } = await readBody(req)
    const user = username ? getUserByName(username) : null
    if (!user || !verifyPassword(password || '', user.password_hash)) {
      sendJson(res, 401, { error: '用户名或密码错误' })
      return true
    }
    sendJson(res, 200, { token: signToken(user.id, user.username), user: { id: user.id, username: user.username } })
    return true
  }

  // ---- 设备 ----
  if (path === '/api/devices' && method === 'GET') {
    const auth = requireAuth(req, res)
    if (!auth) return true
    const devices = getDevicesByUser(auth.userId).map((d) => ({
      device_token: d.device_token,
      name: d.name,
      online: isDeviceOnline(d.device_token),
      last_seen_at: d.last_seen_at,
      last_ip: d.last_ip,
    }))
    sendJson(res, 200, { devices })
    return true
  }

  if (path === '/api/devices' && method === 'POST') {
    const auth = requireAuth(req, res)
    if (!auth) return true
    const { name } = await readBody(req)
    const token = randomBytes(16).toString('hex')   // 32 位十六进制
    const device = registerDevice(auth.userId, token, name || '')
    sendJson(res, 200, { device_token: device.device_token, name: device.name })
    return true
  }

  // ---- 映射云端同步 ----
  if (path === '/api/mappings' && method === 'GET') {
    const auth = requireAuth(req, res)
    if (!auth) return true
    const row = getMapping(auth.userId)
    sendJson(res, 200, row || { profile: null, updatedAt: null })
    return true
  }

  if (path === '/api/mappings' && method === 'POST') {
    const auth = requireAuth(req, res)
    if (!auth) return true
    const { profile } = await readBody(req)
    if (!profile) {
      sendJson(res, 400, { error: '缺少 profile' })
      return true
    }
    const saved = saveMapping(auth.userId, typeof profile === 'string' ? profile : JSON.stringify(profile))
    sendJson(res, 200, saved)
    return true
  }

  return false
}

const server = createServer(async (req, res) => {
  try {
    if (req.url.startsWith('/api/')) {
      const handled = await handleApi(req, res)
      if (!handled) sendJson(res, 404, { error: 'Not Found' })
    } else {
      serveStatic(req, res)
    }
  } catch (err) {
    console.error('[server] 处理失败:', err)
    sendJson(res, 500, { error: '服务器内部错误' })
  }
})

setupBridge(server)

server.listen(PORT, () => {
  console.log(`[server] VibeKey 后端已启动`)
  console.log(`[server]   HTTP + 静态文件: http://localhost:${PORT}`)
  console.log(`[server]   WebSocket 设备端: ws://localhost:${PORT}/ws/device?token=<device_token>`)
  console.log(`[server]   WebSocket 浏览器端: ws://localhost:${PORT}/ws/client?token=<jwt>&device=<device_token>`)
  console.log(`[server]   静态目录: ${STATIC_DIR}`)
})
