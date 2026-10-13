/**
 * REST API 客户端——用户账号 / 设备管理 / 映射云端同步。
 *
 * 与 web/server/index.js 的 REST API 对应。
 * base URL 默认同源（部署时前后端同域），可用 setBase 覆盖（开发时后端在 :8080）。
 */

let base = ''

export function setBase(url) {
  base = url.replace(/\/$/, '')
}

async function req(path, { method = 'GET', body, token } = {}) {
  const res = await fetch(`${base}${path}`, {
    method,
    headers: {
      ...(body ? { 'Content-Type': 'application/json' } : {}),
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    ...(body ? { body: JSON.stringify(body) } : {}),
  })
  const data = await res.json().catch(() => ({}))
  if (!res.ok) {
    throw new Error(data.error || `请求失败 ${res.status}`)
  }
  return data
}

export async function register(username, password) {
  return req('/api/auth/register', { method: 'POST', body: { username, password } })
}

export async function login(username, password) {
  return req('/api/auth/login', { method: 'POST', body: { username, password } })
}

export async function listDevices(token) {
  return req('/api/devices', { token })
}

export async function addDevice(token, name) {
  return req('/api/devices', { method: 'POST', body: { name }, token })
}

export async function getMappings(token) {
  return req('/api/mappings', { token })
}

export async function saveMappings(token, profile) {
  return req('/api/mappings', { method: 'POST', body: { profile }, token })
}
