/**
 * SQLite 数据层——用户、设备、映射的持久化。
 *
 * 用 better-sqlite3（同步、零配置、单文件）。
 * 数据文件：server/data/vibekey.db（由环境变量 DB_PATH 覆盖）。
 */
import Database from 'better-sqlite3'
import { mkdirSync } from 'fs'
import { dirname, join } from 'path'
import { fileURLToPath } from 'url'

const __dirname = dirname(fileURLToPath(import.meta.url))
const DB_PATH = process.env.DB_PATH || join(__dirname, 'data', 'vibekey.db')

mkdirSync(dirname(DB_PATH), { recursive: true })

export const db = new Database(DB_PATH)
db.pragma('journal_mode = WAL')

db.exec(`
  CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at    INTEGER NOT NULL DEFAULT (unixepoch())
  );

  CREATE TABLE IF NOT EXISTS devices (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_token TEXT NOT NULL UNIQUE,
    name         TEXT NOT NULL DEFAULT '',
    last_seen_at INTEGER,
    last_ip      TEXT,
    created_at   INTEGER NOT NULL DEFAULT (unixepoch())
  );

  CREATE TABLE IF NOT EXISTS mappings (
    user_id    INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    profile    TEXT NOT NULL,
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
  );

  CREATE INDEX IF NOT EXISTS idx_devices_user ON devices(user_id);
`)

// ---- 用户 ----
const qInsertUser = db.prepare(
  'INSERT INTO users (username, password_hash) VALUES (?, ?)'
)
const qUserByName = db.prepare('SELECT * FROM users WHERE username = ?')

export function createUser(username, passwordHash) {
  const r = qInsertUser.run(username, passwordHash)
  return qUserByName.get(username) ?? { id: r.lastInsertRowid, username }
}

export function getUserByName(username) {
  return qUserByName.get(username)
}

// ---- 设备 ----
const qInsertDevice = db.prepare(
  'INSERT INTO devices (user_id, device_token, name) VALUES (?, ?, ?)'
)
const qDeviceByToken = db.prepare('SELECT * FROM devices WHERE device_token = ?')
const qDevicesByUser = db.prepare('SELECT * FROM devices WHERE user_id = ?')
const qTouchDevice = db.prepare(
  'UPDATE devices SET last_seen_at = unixepoch(), last_ip = ? WHERE device_token = ?'
)

export function registerDevice(userId, deviceToken, name = '') {
  qInsertDevice.run(userId, deviceToken, name)
  return qDeviceByToken.get(deviceToken)
}

export function getDeviceByToken(deviceToken) {
  return qDeviceByToken.get(deviceToken)
}

export function getDevicesByUser(userId) {
  return qDevicesByUser.all(userId)
}

export function touchDevice(deviceToken, ip) {
  qTouchDevice.run(ip || null, deviceToken)
}

// ---- 映射 ----
const qGetMapping = db.prepare('SELECT profile, updated_at FROM mappings WHERE user_id = ?')
const qUpsertMapping = db.prepare(`
  INSERT INTO mappings (user_id, profile, updated_at) VALUES (?, ?, unixepoch())
  ON CONFLICT(user_id) DO UPDATE SET profile = excluded.profile, updated_at = unixepoch()
`)

export function getMapping(userId) {
  const row = qGetMapping.get(userId)
  return row ? { profile: row.profile, updatedAt: row.updated_at } : null
}

export function saveMapping(userId, profile) {
  qUpsertMapping.run(userId, profile)
  return getMapping(userId)
}
