/**
 * 用户账号——注册 / 登录 / JWT 签发与验证。
 *
 * 不引 jsonwebtoken：用 HMAC-SHA256 自己签一个紧凑的 token，
 * 避免多一个依赖。密钥从环境变量 JWT_SECRET 读（生产必须改）。
 * 密码哈希用 PBKDF2（Node 内置 crypto，不引 bcrypt）。
 */
import { createHmac, pbkdf2Sync, randomBytes, timingSafeEqual } from 'crypto'

const JWT_SECRET = process.env.JWT_SECRET || 'vibekey-dev-secret-change-me'
const TOKEN_TTL_MS = 30 * 24 * 3600 * 1000   // 30 天

if (process.env.NODE_ENV === 'production' && JWT_SECRET === 'vibekey-dev-secret-change-me') {
  console.warn('[auth] ⚠️  生产环境请设 JWT_SECRET 环境变量，否则 token 可被伪造')
}

// ---- 密码哈希（PBKDF2-SHA256） ----
export function hashPassword(password) {
  const salt = randomBytes(16).toString('hex')
  const hash = pbkdf2Sync(password, salt, 100000, 32, 'sha256').toString('hex')
  return `${salt}:${hash}`
}

export function verifyPassword(password, stored) {
  const [salt, hash] = stored.split(':')
  if (!salt || !hash) return false
  const candidate = pbkdf2Sync(password, salt, 100000, 32, 'sha256').toString('hex')
  return timingSafeEqual(Buffer.from(hash, 'hex'), Buffer.from(candidate, 'hex'))
}

// ---- JWT（HS256，自己实现） ----
function b64url(buf) {
  return Buffer.from(buf).toString('base64url')
}

function b64urlJson(obj) {
  return b64url(JSON.stringify(obj))
}

export function signToken(userId, username) {
  const header = b64urlJson({ alg: 'HS256', typ: 'JWT' })
  const payload = b64urlJson({
    sub: userId,
    username,
    exp: Date.now() + TOKEN_TTL_MS,
  })
  const sig = createHmac('sha256', JWT_SECRET)
    .update(`${header}.${payload}`)
    .digest('base64url')
  return `${header}.${payload}.${sig}`
}

export function verifyToken(token) {
  if (typeof token !== 'string') return null
  const parts = token.split('.')
  if (parts.length !== 3) return null
  const [header, payload, sig] = parts

  const expected = createHmac('sha256', JWT_SECRET)
    .update(`${header}.${payload}`)
    .digest('base64url')
  if (!timingSafeEqual(Buffer.from(sig), Buffer.from(expected))) {
    return null
  }

  try {
    const obj = JSON.parse(Buffer.from(payload, 'base64url').toString('utf8'))
    if (typeof obj.exp !== 'number' || obj.exp < Date.now()) return null
    return { userId: obj.sub, username: obj.username }
  } catch {
    return null
  }
}

/** Express 风格的鉴权中间件（原生 http 也能用）。 */
export function requireAuth(req, res) {
  const auth = req.headers.authorization || ''
  const token = auth.startsWith('Bearer ') ? auth.slice(7) : null
  const payload = verifyToken(token)
  if (!payload) {
    res.writeHead(401, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ error: '未登录或 token 已过期' }))
    return null
  }
  return payload
}
