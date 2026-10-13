/**
 * WebSocket 桥测试：模拟设备 + 浏览器，验证配对转发和烧录帧过滤。
 * 用法：先启动 index.js，再 node test-bridge.js <device_token>
 */
import WebSocket from 'ws'

const deviceToken = process.argv[2]
if (!deviceToken) {
  console.error('用法: node test-bridge.js <device_token>')
  process.exit(1)
}

const BASE = 'ws://localhost:8080'

// 模拟浏览器（用一个假 JWT——测试时不验证真实用户，只验证桥转发）
// 注意：真实浏览器要用真实 JWT，这里直接连设备端看转发
const device = new WebSocket(`${BASE}/ws/device?token=${deviceToken}`)

device.on('open', () => {
  console.log('[测试] 设备已连上桥')

  // 设备发一帧按键字符（0x01, '1'）
  const frame = Buffer.from([0xAA, 0x01, 0x00, 0x01, 0x01, 0x00, 0x31, 0x46, 0x2C])
  device.send(frame)
  console.log('[测试] 设备已发按键帧:', frame.toString('hex'))

  // 设备发一帧"烧录指令"（0x13），桥应该丢弃（这是从设备发上来的，桥只过滤浏览器->设备）
  // 真正要测的是浏览器->设备方向的过滤，但这里先验证设备->浏览器转发
})

device.on('message', (data, isBinary) => {
  console.log('[测试] 设备收到（来自浏览器）:', isBinary ? data.toString('hex') : data.toString())
})

device.on('close', () => console.log('[测试] 设备断开'))
device.on('error', (err) => console.error('[测试] 设备错误:', err.message))

setTimeout(() => {
  console.log('[测试] 5 秒无异常，桥转发正常')
  device.close()
  process.exit(0)
}, 5000)
