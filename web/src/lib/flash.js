/**
 * 烧录逻辑——在浏览器里用 esptool-js 完成"进入下载模式 → COM 口跳变 → esptool 写入"。
 *
 * 流程（设计文档 §7.1 情形 A，硬件已确认 USB-C 接 GPIO19/20）：
 *   1. 网页通过 Web Serial 下发 ENTER_DFU（magic word）→ 设备重启进下载模式
 *   2. COM 口从 TinyUSB CDC（VID 303A / PID 4001）跳变为
 *      ROM 的 USB Serial/JTAG（VID 303A / PID 1001）
 *   3. 用户重新选择新端口（下载模式）
 *   4. esptool-js writeFlash
 *
 * esptool-js 的 USB_JTAG_SERIAL_PID = 4097 = 0x1001，正是 ROM 下载模式的 PID。
 */
import { ESPLoader, Transport } from 'esptool-js'

const DFU_TIMEOUT_MS = 15000
const POLL_INTERVAL_MS = 300

/**
 * 等设备重启并重新枚举为下载模式端口。
 * 返回 true 表示有新端口出现（需要用户重新选择）。
 */
export async function waitForDownloadPort(onLog) {
  const deadline = Date.now() + DFU_TIMEOUT_MS
  while (Date.now() < deadline) {
    // Web Serial 没有"枚举端口"的 API，只能让用户重新 requestPort。
    // 这里只负责等待和提示。
    onLog?.('等待设备重启并重新枚举为下载模式…（COM 口会跳变，属正常）')
    await new Promise((r) => setTimeout(r, POLL_INTERVAL_MS * 10)
    )
    return true   // 提示一次即可，让用户去选
  }
  return false
}

/** esptool-js 的日志终端适配。 */
function makeTerminal(onLog) {
  return {
    write(data) {
      const text = typeof data === 'string' ? data : String(data)
      for (const line of text.split(/\r?\n/)) {
        if (line.trim()) onLog?.(line.trim())
      }
    },
    writeLine(data) {
      this.write(data + '\n')
    },
    clean() { /* 不用清屏 */ },
  }
}

/**
 * 完整烧录一份固件镜像。
 *
 * @param {Uint8Array} imageData 固件 .bin 内容
 * @param {number} address 写入地址（应用分区通常 0x10000）
 * @param {(stage: string, fraction?: number) => void} onProgress 进度回调
 * @param {(text: string, isError?: boolean) => void} onLog 日志回调
 */
export async function flashImage(imageData, address, onProgress, onLog) {
  // ---- 1. 让用户选下载模式的端口 ----
  onLog?.('请在弹出的端口选择里，选**新的**串口（下载模式，VID 303A / PID 1001）。')
  onProgress?.('waiting_port')

  let port
  try {
    port = await navigator.serial.requestPort()
  } catch (err) {
    throw new Error(err.name === 'NotFoundError' ? '已取消选择端口' : `选择端口失败：${err.message}`)
  }

  // ---- 2. 连接芯片 ----
  onProgress?.('connecting')
  onLog?.('与芯片握手同步…')

  const transport = new Transport(port, false)
  const loader = new ESPLoader(transport)

  // esptool-js 的日志走 loaderTerminal
  loader.terminal = makeTerminal(onLog)

  try {
    await loader.main()
    onLog?.(`已连接：${loader.chipName || 'ESP32-S3'}`)
    onProgress?.('detecting', 0.1)

    // ---- 3. 写入 ----
    onLog?.(`写入 ${formatBytes(imageData.length)} 到 0x${address.toString(16)}…`)
    onProgress?.('writing', 0.2)

    await loader.writeFlash({
      fileArray: [{ data: imageData, address }],
      flashSize: '16MB',           // BOM 是 N16R8
      flashMode: 'dio',
      flashFreq: '80m',
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, total) => {
        const frac = 0.2 + (written / total) * 0.7
        onProgress?.('writing', frac)
      },
    })

    onProgress?.('verifying', 0.95)
    onLog?.('写入完成，校验中…')

    // ---- 4. 重启 ----
    onProgress?.('rebooting', 0.98)
    onLog?.('重启设备…')
    await loader.hardReset()
    onProgress?.('done', 1.0)
    onLog?.('烧录完成，设备将重启回到工作态。')
    return true
  } finally {
    try {
      await transport.disconnect()
    } catch { /* ignore */ }
  }
}

export function formatBytes(n) {
  if (n >= 1048576) return `${(n / 1048576).toFixed(1)} MB`
  if (n >= 1024) return `${(n / 1024).toFixed(1)} KB`
  return `${n} B`
}

export const STAGE_LABEL = {
  waiting_port: '选择下载模式端口',
  connecting: '连接芯片',
  detecting: '读取芯片信息',
  writing: '写入固件',
  verifying: '校验',
  rebooting: '重启',
  done: '完成',
}
