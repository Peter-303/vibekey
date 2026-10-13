/**
 * 端到端测试：真实 JWT + 设备 + 浏览器，验证双向转发和烧录帧过滤。
 * 用法：node test-e2e.js
 */
import WebSocket from 'ws'

const BASE_HTTP = 'http://localhost:8080'
const BASE_WS = 'ws://localhost:8080'

async function post(path, body, token) {
  const res = await fetch(`${BASE_HTTP}${path}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: JSON.stringify(body),
  })
  return res.json()
}

async function main() {
  // 1. 注册 + 登录
  const username = `e2e_${Date.now()}`
  await post('/api/auth/register', { username, password: 'test123' })
  const { token: jwt } = await post('/api/auth/login', { username, password: 'test123' })
  console.log('[1] 登录 OK')

  // 2. 创建设备
  const { device_token: deviceToken } = await post('/api/devices', { name: 'e2e 键盘' }, jwt)
  console.log('[2] 创建设备 OK:', deviceToken.slice(0, 12) + '…')

  const results = { deviceOnline: false, keyForwarded: false, cmdForwarded: false, dfuBlocked: false }
  const done = new Promise((resolve) => setTimeout(resolve, 4000))

  // 3. 浏览器连接
  const client = new WebSocket(`${BASE_WS}/ws/client?token=${jwt}&device=${deviceToken}`)
  client.on('message', (data, isBinary) => {
    if (isBinary) {
      const hex = Buffer.from(data).toString('hex')
      console.log('[浏览器] 收到设备帧:', hex)
      if (hex.includes('aa010001010031462c')) results.keyForwarded = true
    } else {
      const msg = JSON.parse(data.toString())
      console.log('[浏览器] 收到消息:', msg)
      if (msg.type === 'device_online') results.deviceOnline = true
      if (msg.type === 'warn' && msg.message.includes('烧录')) results.dfuBlocked = true
    }
  })

  await new Promise((r) => client.on('open', r))
  console.log('[3] 浏览器已连接')

  // 4. 设备连接
  const device = new WebSocket(`${BASE_WS}/ws/device?token=${deviceToken}`)
  device.on('message', (data, isBinary) => {
    const hex = Buffer.from(data).toString('hex')
    console.log('[设备] 收到浏览器帧:', hex)
    if (hex.includes('aa000010e80317fb')) results.cmdForwarded = true   // ping 帧
  })

  await new Promise((r) => device.on('open', r))
  console.log('[4] 设备已连接')

  await new Promise((r) => setTimeout(r, 500))

  // 5. 设备发按键帧 → 浏览器应收到
  device.send(Buffer.from([0xAA, 0x01, 0x00, 0x01, 0x01, 0x00, 0x31, 0x46, 0x2C]))
  console.log('[5] 设备已发按键帧')

  await new Promise((r) => setTimeout(r, 500))

  // 6. 浏览器发 ping 帧 → 设备应收到
  client.send(Buffer.from([0xAA, 0x00, 0x00, 0x10, 0xE8, 0x03, 0x17, 0xFB]))
  console.log('[6] 浏览器已发 ping 帧')

  await new Promise((r) => setTimeout(r, 500))

  // 7. 浏览器发烧录帧 → 应被过滤，设备收不到，浏览器收到警告
  client.send(Buffer.from([0xAA, 0x08, 0x00, 0x13, 0x01, 0x00, 0x56, 0x49, 0x42, 0x45, 0x46, 0x4C, 0x41, 0x53, 0x00, 0x00]))
  console.log('[7] 浏览器已发烧录帧（应被过滤）')

  await done

  client.close()
  device.close()

  console.log('\n=== 结果 ===')
  console.log('设备上线通知:', results.deviceOnline ? '✅' : '❌')
  console.log('按键帧转发（设备→浏览器）:', results.keyForwarded ? '✅' : '❌')
  console.log('指令帧转发（浏览器→设备）:', results.cmdForwarded ? '✅' : '❌')
  console.log('烧录帧被过滤:', results.dfuBlocked ? '✅' : '❌')

  const allPass = Object.values(results).every(Boolean)
  console.log(allPass ? '\n全部通过' : '\n有失败')
  process.exit(allPass ? 0 : 1)
}

main().catch((err) => {
  console.error('测试失败:', err)
  process.exit(1)
})
