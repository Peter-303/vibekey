import { useEffect, useRef, useState } from 'react'
import { SerialDevice, statusLabel } from '../lib/serial.js'
import { T, payloadText } from '../lib/frame.js'
import { flashImage, formatBytes, STAGE_LABEL } from '../lib/flash.js'
import { CloudDevice } from '../lib/cloud.js'
import * as api from '../lib/api.js' 

const ACTION_TYPES = [
  { value: 'text', label: '输入文本' },
  { value: 'hotkey', label: '触发快捷键' },
  { value: 'open', label: '打开程序 / URL / 文件' },
  { value: 'voice', label: '语音输入' },
  { value: 'clipboard', label: '复制到剪贴板' },
]

const DEFAULT_KEYS = [
  { key: '1', label: 'K1' },
  { key: '2', label: 'K2' },
  { key: '3', label: 'K3' },
  { key: '4', label: 'K4' },
  { key: '5', label: 'K5' },
  { key: '6', label: 'K6' },
  { key: '7', label: 'K7（建议语音键）' },
  { key: '+', label: '旋钮 右旋' },
  { key: '-', label: '旋钮 左旋' },
  { key: '0', label: '旋钮 按下' },
]

function Section({ title, children, accent = false }) {
  return (
    <div className={`${accent ? 'card-brutal-accent' : 'card-brutal'} p-5`}>
      <h3 className="mb-4 text-base font-bold">{title}</h3>
      {children}
    </div>
  )
}

export default function ConfigTool({ onBack }) {
  const usbRef = useRef(null)
  if (!usbRef.current) {
    usbRef.current = new SerialDevice()
  }
  const cloudRef = useRef(null)
  if (!cloudRef.current) {
    cloudRef.current = new CloudDevice()
  }

  const [channel, setChannel] = useState('usb')   // 'usb' | 'cloud'
  const [jwt, setJwt] = useState(() => localStorage.getItem('vibekey_jwt') || '')
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [devices, setDevices] = useState([])
  const [selDevice, setSelDevice] = useState('')
  const [wsBase, setWsBase] = useState(() => localStorage.getItem('vibekey_ws') || 'wss://vibekey.bealux.tech')

  const dev = channel === 'usb' ? usbRef.current : cloudRef.current

  const [state, setState] = useState('disconnected')
  const [detail, setDetail] = useState('')
  const [status, setStatus] = useState({ state: 'BOOT', fw: '', ip: '', rssi: 0, asr: '', warn: 'NONE' })
  const [logs, setLogs] = useState([])
  const [ssid, setSsid] = useState('')
  const [pass, setPass] = useState('')
  const [provisioning, setProvisioning] = useState(false)
  const [keys, setKeys] = useState(() =>
    DEFAULT_KEYS.map((k) => ({ ...k, action: 'text', text: '', hotkey: '', target: '', enabled: true }))
  )
  const [selKey, setSelKey] = useState(DEFAULT_KEYS[0].key)
  const [flashFile, setFlashFile] = useState(null)
  const [flashing, setFlashing] = useState(false)
  const [flashStage, setFlashStage] = useState('')
  const [flashProgress, setFlashProgress] = useState(0)

  const addLog = (text, isError = false) => {
    const line = { t: new Date().toLocaleTimeString('zh-CN', { hour12: false }), text, isError }
    setLogs((prev) => [line, ...prev].slice(0, 200))
  }

  useEffect(() => {
    dev.onStateChange = (s, d) => {
      setState(s)
      setDetail(d || '')
      if (s === 'connected') {
        addLog('设备已连接')
        dev.requestStatus()
      }
    }
    dev.onLog = addLog
    dev.onFrame = (f) => {
      switch (f.type) {
        case T.KEY_EVENT: {
          const ch = String.fromCharCode(f.payload[0])
          addLog(`按键 '${ch}'`)
          break
        }
        case T.STATUS: {
          const text = payloadText(f.payload)
          parseStatus(text)
          addLog(`状态：${text}`)
          break
        }
        case T.PONG:
          setState('connected')
          break
        case T.ACK: {
          const ok = f.payload[0] === 0
          const reason = f.payload.length > 1 ? payloadText(f.payload.subarray(1)) : ''
          addLog(ok ? '指令已接受' : `指令被拒绝：${reason}`, !ok)
          break
        }
        case T.ASR_TEXT: {
          const isFinal = f.payload[0] === 1
          const text = payloadText(f.payload.subarray(1))
          addLog(`ASR${isFinal ? '（最终）' : '（中间）'}：${text}`)
          break
        }
        default:
          break
      }
    }
    return () => {
      dev.onStateChange = null
      dev.onLog = null
      dev.onFrame = null
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const parseStatus = (text) => {
    const parts = text.split(',')
    const next = { state: parts[0] || 'BOOT', fw: '', ip: '', rssi: 0, asr: '', warn: 'NONE' }
    for (const p of parts.slice(1)) {
      const eq = p.indexOf('=')
      if (eq <= 0) continue
      const k = p.slice(0, eq)
      const v = p.slice(eq + 1)
      if (k === 'FW') next.fw = v
      else if (k === 'IP') next.ip = v
      else if (k === 'RSSI') next.rssi = parseInt(v, 10) || 0
      else if (k === 'ASR') next.asr = v
      else if (k === 'WARN') next.warn = v
      else if (k === 'ERR') next.warn = v
    }
    setStatus(next)
  }

  // ---- 云：登录 / 注册 ----
  const doAuth = async (isLogin) => {
    if (!username.trim() || password.length < 6) {
      addLog('用户名至少 2 位，密码至少 6 位', true)
      return
    }
    try {
      api.setBase(`http://${location.hostname}:8080`)
      const r = isLogin
        ? await api.login(username.trim(), password)
        : await api.register(username.trim(), password)
      setJwt(r.token)
      localStorage.setItem('vibekey_jwt', r.token)
      addLog(`${isLogin ? '登录' : '注册'}成功：${r.user.username}`)
      loadDevices(r.token)
    } catch (err) {
      addLog(`${isLogin ? '登录' : '注册'}失败：${err.message}`, true)
    }
  }

  const doLogout = () => {
    setJwt('')
    localStorage.removeItem('vibekey_jwt')
    setDevices([])
    setSelDevice('')
    cloudRef.current.disconnect()
    addLog('已退出登录')
  }

  // ---- 云：设备列表 ----
  const loadDevices = async (token) => {
    try {
      api.setBase(`http://${location.hostname}:8080`)
      const r = await api.listDevices(token || jwt)
      setDevices(r.devices || [])
      if ((r.devices || []).length > 0 && !selDevice) {
        setSelDevice(r.devices[0].device_token)
      }
    } catch (err) {
      addLog(`加载设备列表失败：${err.message}`, true)
    }
  }

  const doAddDevice = async () => {
    const name = window.prompt('给这台键盘起个名字（可选）：', '我的 VibeKey')
    if (name === null) return
    try {
      api.setBase(`http://${location.hostname}:8080`)
      const r = await api.addDevice(jwt, name)
      addLog(`已添加设备：${r.name || '未命名'}，token=${r.device_token.slice(0, 12)}…`)
      addLog('请把 cloud_url 和 cloud_token 通过配网写入设备（见下方"云配置"）')
      loadDevices()
    } catch (err) {
      addLog(`添加设备失败：${err.message}`, true)
    }
  }

  // ---- 云：连桥 ----
  const connectCloud = () => {
    if (!jwt) {
      addLog('请先登录', true)
      return
    }
    if (!selDevice) {
      addLog('请先添加一台设备', true)
      return
    }
    localStorage.setItem('vibekey_ws', wsBase)
    cloudRef.current.connect(wsBase, jwt, selDevice)
  }

  const sendCloudConfig = async () => {
    if (!selDevice) {
      addLog('请先选一台设备', true)
      return
    }
    const url = 'wss://vibekey.bealux.tech/ws/device'
    addLog(`下发云配置：cloud_url=${url}，cloud_token=${selDevice.slice(0, 12)}…`)
    const ok = await dev.sendCloudConfig(url, selDevice)
    addLog(ok ? '云配置已下发，设备将保存并主动连云' : '下发失败', !ok)
  }

  const startProvision = async () => {
    if (!ssid.trim()) {
      addLog('请先填 WiFi 名称（SSID）', true)
      return
    }
    if (pass.length < 8) {
      addLog('密码至少 8 位', true)
      return
    }
    setProvisioning(true)
    addLog(`下发 WiFi 凭据：${ssid}`)
    const ok = await dev.sendWifiConfig(ssid.trim(), pass)
    addLog(ok ? '已下发，设备将重启并连上家庭 WiFi（约 10s 后蓝灯常亮）' : '下发失败', !ok)
    setProvisioning(false)
  }

  const sel = keys.find((k) => k.key === selKey) || keys[0]
  const updateSel = (patch) => {
    setKeys((prev) => prev.map((k) => (k.key === selKey ? { ...k, ...patch } : k)))
  }

  const onPickFirmware = (e) => {
    const f = e.target.files?.[0]
    if (f) {
      setFlashFile(f)
      addLog(`已选择固件：${f.name}（${formatBytes(f.size)}）`)
    }
  }

  const startFlashing = async () => {
    if (!flashFile) {
      addLog('请先选择固件镜像（.bin）', true)
      return
    }
    if (!window.confirm(
      `这将擦除当前固件并烧录新固件。\n\n` +
      `镜像：${flashFile.name}（${formatBytes(flashFile.size)}）\n` +
      `过程：进入下载模式 -> 擦除 -> 写入 -> 校验 -> 重启\n\n` +
      `烧录过程中请勿拔 USB 线。确定继续？`
    )) {
      return
    }

    setFlashing(true)
    setFlashProgress(0)
    setFlashStage('')

    try {
      // 1. 如果设备连着（工作态），先下发进入下载模式指令
      if (dev.connected) {
        addLog('第 1 步：请求设备进入下载模式…')
        const ok = await dev.enterDfu()
        if (ok) {
          addLog('已下发指令，设备将重启并重新枚举（COM 口跳变，属正常）')
          await new Promise((r) => setTimeout(r, 2000))
          await dev.disconnect()
        } else {
          addLog('下发失败，请检查设备连接', true)
          setFlashing(false)
          return
        }
      } else {
        addLog('设备未连接——如果它已在下载模式，请直接选新端口；否则先「连接设备」')
      }

      // 2. 读镜像并烧录（用户选下载模式的新端口）
      addLog('第 2 步：选择下载模式的串口…')
      const buf = new Uint8Array(await flashFile.arrayBuffer())
      await flashImage(buf, 0x10000, (stage, frac) => {
        setFlashStage(stage)
        if (typeof frac === 'number') setFlashProgress(frac)
      }, addLog)

      addLog('烧录完成！设备将重启回到工作态。')
      setFlashStage('done')
      setFlashProgress(1)
    } catch (err) {
      addLog(`烧录失败：${err.message}`, true)
      setFlashStage('error')
    } finally {
      setFlashing(false)
    }
  }

  const downloadMappings = () => {
    const profile = {
      version: 1,
      name: '网页配置',
      mappings: keys.map(({ key, label, action, text, hotkey, target, enabled }) => ({
        key, label, action, text, hotkey, target, enabled,
      })),
    }
    const blob = new Blob([JSON.stringify(profile, null, 2)], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'vibekey-mappings.json'
    a.click()
    URL.revokeObjectURL(url)
    addLog('映射配置已下载，可在 Windows 上位机里导入')
  }

  return (
    <main className="mx-auto max-w-5xl px-6 py-8">
      <button onClick={onBack} className="mb-4 text-sm font-semibold text-ink-muted hover:text-pink">
        ← 返回首页
      </button>

      {/* 连接方式切换 */}
      <div className="mb-4 flex gap-2">
        <button
          onClick={() => setChannel('usb')}
          className={`btn-brutal px-4 py-2 text-sm ${channel === 'usb' ? '' : 'btn-brutal-secondary'}`}
        >
          USB 直连
        </button>
        <button
          onClick={() => setChannel('cloud')}
          className={`btn-brutal px-4 py-2 text-sm ${channel === 'cloud' ? '' : 'btn-brutal-secondary'}`}
        >
          云端连接（WiFi）
        </button>
      </div>

      {/* 连接条 */}
      <div className="card-brutal mb-6 flex flex-wrap items-center gap-4 p-4">
        <span
          className={`inline-block h-3 w-3 rounded-full border-2 border-ink ${
            state === 'connected' ? 'bg-ok' : state === 'error' ? 'bg-bad' : 'bg-ink-faint'
          }`}
        />
        <span className="font-bold">{statusLabel(state)}</span>
        {detail && <span className="text-sm text-ink-muted">{detail}</span>}
        <div className="ml-auto flex gap-2">
          {state === 'connected' ? (
            <button onClick={() => dev.disconnect()} className="btn-brutal btn-brutal-secondary px-4 py-2 text-sm">
              断开
            </button>
          ) : channel === 'usb' ? (
            <button onClick={() => dev.connect()} className="btn-brutal px-4 py-2 text-sm">
              连接设备
            </button>
          ) : (
            <button onClick={connectCloud} className="btn-brutal px-4 py-2 text-sm">
              连云
            </button>
          )}
          {state === 'connected' && (
            <button onClick={() => dev.requestStatus()} className="btn-brutal btn-brutal-secondary px-4 py-2 text-sm">
              刷新状态
            </button>
          )}
        </div>
      </div>

      {!dev.supported && (
        <div className="card-brutal-accent mb-6 p-4 text-sm font-semibold">
          当前浏览器不支持 Web Serial API。请用 Chrome 或 Edge 打开这个页面。
        </div>
      )}

      {channel === 'cloud' && (
        <div className="mb-6 grid gap-5 lg:grid-cols-2">
          {/* 登录 */}
          <Section title="登录 / 注册">
            {jwt ? (
              <div className="flex items-center gap-3 text-sm">
                <span className="inline-block h-2.5 w-2.5 rounded-full bg-ok" />
                <span className="font-semibold">已登录</span>
                <button onClick={doLogout} className="btn-brutal btn-brutal-secondary ml-auto px-3 py-1.5 text-xs">
                  退出
                </button>
              </div>
            ) : (
              <>
                <label className="label-brutal">用户名</label>
                <input className="input-brutal mb-3" value={username} onChange={(e) => setUsername(e.target.value)} placeholder="至少 2 位" />
                <label className="label-brutal">密码</label>
                <input className="input-brutal mb-4" type="password" value={password} onChange={(e) => setPassword(e.target.value)} placeholder="至少 6 位" />
                <div className="flex gap-2">
                  <button onClick={() => doAuth(true)} className="btn-brutal text-sm">登录</button>
                  <button onClick={() => doAuth(false)} className="btn-brutal btn-brutal-secondary text-sm">注册</button>
                </div>
              </>
            )}
          </Section>

          {/* 设备列表 */}
          <Section title="我的设备">
            {jwt ? (
              <>
                <div className="mb-3 flex items-center gap-2">
                  <select
                    className="input-brutal flex-1"
                    value={selDevice}
                    onChange={(e) => setSelDevice(e.target.value)}
                  >
                    <option value="">选一台设备</option>
                    {devices.map((d) => (
                      <option key={d.device_token} value={d.device_token}>
                        {d.online ? '🟢 ' : '⚪ '}{d.name || '未命名'}（{d.device_token.slice(0, 10)}…）
                      </option>
                    ))}
                  </select>
                  <button onClick={doAddDevice} className="btn-brutal btn-brutal-secondary px-3 py-2 text-xs">
                    + 添加
                  </button>
                </div>
                {selDevice && (
                  <div className="mb-3">
                    <label className="label-brutal">云桥地址（WebSocket）</label>
                    <input className="input-brutal" value={wsBase} onChange={(e) => setWsBase(e.target.value)} placeholder="ws://localhost:8080" />
                    <button onClick={sendCloudConfig} className="btn-brutal btn-brutal-secondary mt-2 text-xs">
                      下发云配置到设备（让它主动连云）
                    </button>
                  </div>
                )}
                <button onClick={() => loadDevices()} className="text-xs text-ink-muted underline decoration-pink underline-offset-2 hover:text-pink">
                  刷新列表
                </button>
              </>
            ) : (
              <div className="text-sm text-ink-faint">登录后管理你的键盘</div>
            )}
          </Section>
        </div>
      )}

      <div className="grid gap-5 lg:grid-cols-2">
        {/* 左列 */}
        <div className="space-y-5">
          <Section title="设备状态">
            <div className="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
              <span className="text-ink-muted">状态</span>
              <span className="font-semibold">{status.state}</span>
              <span className="text-ink-muted">固件</span>
              <span>{status.fw || '—'}</span>
              <span className="text-ink-muted">IP</span>
              <span>{status.ip || '—'}</span>
              <span className="text-ink-muted">信号</span>
              <span>{status.rssi ? `${status.rssi} dBm` : '—'}</span>
              <span className="text-ink-muted">语音</span>
              <span>{status.asr || '—'}</span>
              <span className="text-ink-muted">告警</span>
              <span className={status.warn !== 'NONE' ? 'text-warn font-semibold' : ''}>{status.warn}</span>
            </div>
            {state !== 'connected' && (
              <div className="mt-4 rounded-lg border-2 border-ink bg-pink-subtle p-3 text-sm">
                点左上角「连接设备」，选择 VibeKey 的串口（VID 303A / PID 4001）。
              </div>
            )}
          </Section>

          <Section title="配网">
            <p className="mb-3 text-sm text-ink-muted">
              设备处于 AP 配网模式（红蓝交替闪）时，填家庭 WiFi 的凭据下发。
            </p>
            <label className="label-brutal">家庭 WiFi 名称（SSID）</label>
            <input className="input-brutal mb-3" value={ssid} onChange={(e) => setSsid(e.target.value)} placeholder="MyHome" />
            <label className="label-brutal">密码</label>
            <input className="input-brutal mb-4" type="password" value={pass} onChange={(e) => setPass(e.target.value)} placeholder="至少 8 位" />
            <button
              onClick={startProvision}
              disabled={state !== 'connected' || provisioning}
              className="btn-brutal text-sm"
            >
              {provisioning ? '下发中…' : '开始配网'}
            </button>
            <p className="mt-3 text-xs text-ink-faint">
              设备收到凭据后会自动重启并连上家庭 WiFi；蓝灯常亮即配网成功。
            </p>
          </Section>

          <Section title="烧录固件">
            <p className="mb-3 text-sm text-ink-muted">
              {channel === 'cloud'
                ? '烧录只允许 USB 通道（设计文档 §7.1 安全边界）。请切到「USB 直连」烧录。'
                : '烧录走"进入下载模式 → COM 口跳变 → esptool 写入"。整个过程在浏览器里完成，不用装 esptool。'}
            </p>

            <label className="label-brutal">固件镜像（.bin）</label>
            <div className="mb-4 flex items-center gap-3">
              <input
                type="file"
                accept=".bin"
                onChange={onPickFirmware}
                className="text-sm file:mr-3 file:rounded-lg file:border-2 file:border-ink file:bg-card file:px-3 file:py-1.5 file:text-sm file:font-semibold file:shadow-brutal-xs hover:file:bg-pink-subtle"
              />
              {flashFile && (
                <span className="text-xs text-ink-muted">
                  {flashFile.name}（{formatBytes(flashFile.size)}）
                </span>
              )}
            </div>

            <button
              onClick={startFlashing}
              disabled={flashing || !flashFile}
              className="btn-brutal text-sm"
            >
              {flashing ? '烧录中…' : '烧录固件'}
            </button>

            {(flashing || flashStage) && flashStage !== '' && (
              <div className="mt-4">
                <div className="mb-1.5 flex items-center justify-between text-xs text-ink-muted">
                  <span>{STAGE_LABEL[flashStage] || flashStage}</span>
                  <span>{Math.round(flashProgress * 100)}%</span>
                </div>
                <div className="h-2.5 overflow-hidden rounded-full border-2 border-ink bg-divider">
                  <div
                    className="h-full bg-pink transition-all duration-300"
                    style={{ width: `${flashProgress * 100}%` }}
                  />
                </div>
              </div>
            )}

            <p className="mt-3 text-xs text-ink-faint">
              烧录过程中请勿拔 USB 线。设备会重启两次：先进下载模式，写完后回工作态。
            </p>
          </Section>
        </div>

        {/* 右列 */}
        <div className="space-y-5">
          <Section title="按键映射">
            <label className="label-brutal">按键</label>
            <select
              className="input-brutal mb-3"
              value={selKey}
              onChange={(e) => setSelKey(e.target.value)}
            >
              {keys.map((k) => (
                <option key={k.key} value={k.key}>
                  {k.label}
                </option>
              ))}
            </select>

            <label className="label-brutal">动作类型</label>
            <select
              className="input-brutal mb-3"
              value={sel.action}
              onChange={(e) => updateSel({ action: e.target.value })}
            >
              {ACTION_TYPES.map((a) => (
                <option key={a.value} value={a.value}>
                  {a.label}
                </option>
              ))}
            </select>

            {sel.action === 'text' || sel.action === 'clipboard' ? (
              <>
                <label className="label-brutal">{sel.action === 'text' ? '要输入的文本' : '要复制的文本'}</label>
                <input
                  className="input-brutal mb-3"
                  value={sel.text}
                  onChange={(e) => updateSel({ text: e.target.value })}
                  placeholder="例如：帮我重构这个函数"
                />
              </>
            ) : null}

            {sel.action === 'hotkey' ? (
              <>
                <label className="label-brutal">快捷键</label>
                <input
                  className="input-brutal mb-3"
                  value={sel.hotkey}
                  onChange={(e) => updateSel({ hotkey: e.target.value })}
                  placeholder="例如：Ctrl+Shift+P"
                />
              </>
            ) : null}

            {sel.action === 'open' ? (
              <>
                <label className="label-brutal">程序 / URL / 文件路径</label>
                <input
                  className="input-brutal mb-3"
                  value={sel.target}
                  onChange={(e) => updateSel({ target: e.target.value })}
                  placeholder="例如：code 或 https://github.com"
                />
              </>
            ) : null}

            <label className="mb-4 flex items-center gap-2 text-sm">
              <input
                type="checkbox"
                checked={sel.enabled}
                onChange={(e) => updateSel({ enabled: e.target.checked })}
              />
              启用这个按键
            </label>

            <div className="flex gap-2">
              <button onClick={downloadMappings} className="btn-brutal text-sm">
                下载映射配置
              </button>
              <span className="self-center text-xs text-ink-faint">
                在 Windows 上位机里导入即生效
              </span>
            </div>
          </Section>

          <Section title="运行日志">
            <div className="max-h-64 space-y-1 overflow-y-auto rounded-lg border-2 border-ink/10 bg-cream/60 p-3 text-xs">
              {logs.length === 0 ? (
                <div className="text-ink-faint">暂无日志</div>
              ) : (
                logs.map((l, i) => (
                  <div key={i} className={l.isError ? 'text-bad' : 'text-ink-muted'}>
                    <span className="mr-2 text-ink-faint">{l.t}</span>
                    {l.text}
                  </div>
                ))
              )}
            </div>
            <button onClick={() => setLogs([])} className="btn-brutal btn-brutal-secondary mt-3 px-3 py-1.5 text-xs">
              清空
            </button>
          </Section>
        </div>
      </div>
    </main>
  )
}
