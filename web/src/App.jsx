import { useState } from 'react'
import ConfigTool from './components/ConfigTool.jsx'

const DOWNLOAD_URL =
  'https://github.com/Peter-303/vibekey/releases/download/v0.1.0/VibeKey-Setup-0.1.0.exe'
const DOCS_URL = 'https://github.com/Peter-303/vibekey'
const RELEASES_URL = 'https://github.com/Peter-303/vibekey/releases/tag/v0.1.0'

function Hero({ onOpenTool }) {
  return (
    <section className="mx-auto max-w-5xl px-6 pt-16 pb-12 text-center">
      <div className="inline-flex items-center gap-2 rounded-full border-2 border-ink bg-card px-4 py-1 text-sm font-semibold shadow-brutal-sm">
        <span className="inline-block h-2.5 w-2.5 rounded-full bg-pink" />
        萤狸科技 × Datawhale · AI 硬件板块
      </div>

      <h1 className="mt-8 text-5xl font-black leading-tight tracking-tight md:text-6xl">
        Vibe Coding 专用键盘
      </h1>
      <p className="mx-auto mt-5 max-w-2xl text-lg text-ink-muted">
        把 AI 编程中最繁琐、最打断氛围的操作收敛到几个实体按键。
        7 个机械键 + 1 个可按压旋钮，配网、映射、烧录全部在网页里完成——
        不用装驱动，不用装应用。
      </p>

      <div className="mt-10 flex flex-wrap items-center justify-center gap-4">
        <a href={DOWNLOAD_URL} className="btn-brutal text-base">
          下载 Windows 上位机
        </a>
        <button onClick={onOpenTool} className="btn-brutal btn-brutal-secondary text-base">
          打开网页配置工具
        </button>
      </div>

      <p className="mt-5 text-sm text-ink-faint">
        Windows 10/11 · 无需 .NET 运行时 · 无需驱动（自带 usbser.sys）
      </p>
    </section>
  )
}

function Feature({ title, desc, icon }) {
  return (
    <div className="card-brutal p-6 text-left">
      <div className="text-2xl">{icon}</div>
      <h3 className="mt-3 text-lg font-bold">{title}</h3>
      <p className="mt-2 text-sm leading-relaxed text-ink-muted">{desc}</p>
    </div>
  )
}

function Features() {
  return (
    <section className="mx-auto max-w-5xl px-6 pb-12">
      <div className="grid gap-5 md:grid-cols-2 lg:grid-cols-3">
        <Feature
          icon="⌨️"
          title="7 键 + 旋钮，全可自定义"
          desc="每个按键、每次旋转都能映射成任意动作：输入文本、触发快捷键、打开程序、开始录音、发送 Prompt。映射在上位机完成，固件永远只发一个字符。"
        />
        <Feature
          icon="🎙️"
          title="按住说话，边说边出字"
          desc="板载 I2S 数字麦直连腾讯云 ASR，流式转写延迟 < 300ms。按住语音键说话，文字直接进入当前光标位置。"
        />
        <Feature
          icon="🔌"
          title="配网烧录全在网页"
          desc="第一次拿到键盘，插上线打开这个页面就能配网、烧录。不用装驱动，不用装应用，不用碰命令行。"
        />
        <Feature
          icon="🔒"
          title="烧录态唯一入口"
          desc="只有上位机显式下发带 magic word 的指令才会进入下载模式。普通按键、串口误触、WiFi 报文都不能触发，避免误升级。"
        />
        <Feature
          icon="📖"
          title="硬件固件全开源"
          desc="原理图、PCB、BOM、固件全部开放，配套 Datawhale 开源教程。可以自由 fork、改造、叠加功能，形成 AI 硬件创新生态。"
        />
        <Feature
          icon="⚡"
          title="ESP32-S3 + 16MB Flash"
          desc="ESP32-S3-WROOM-1-N16R8，16MB flash + 8MB PSRAM。WiFi + USB 双通道，断线自动重连。"
        />
      </div>
    </section>
  )
}

function HowItWorks() {
  const steps = [
    { n: '1', t: '插上 USB-C', d: 'Windows 自动枚举为虚拟 COM 口，无需装驱动' },
    { n: '2', t: '打开这个页面', d: '点「连接设备」，网页通过 Web Serial 直连键盘' },
    { n: '3', t: '配网', d: '填家里 WiFi 的 SSID 和密码，键盘自动重启连上' },
    { n: '4', t: '开始用', d: '改映射、烧录、看状态，全部在网页里完成' },
  ]
  return (
    <section className="mx-auto max-w-5xl px-6 pb-14">
      <h2 className="mb-6 text-center text-3xl font-black">四步上手</h2>
      <div className="grid gap-5 md:grid-cols-4">
        {steps.map((s) => (
          <div key={s.n} className="card-brutal-accent p-5">
            <div className="flex h-9 w-9 items-center justify-center rounded-full border-2 border-ink bg-card text-lg font-black shadow-brutal-xs">
              {s.n}
            </div>
            <h3 className="mt-3 font-bold">{s.t}</h3>
            <p className="mt-1 text-sm text-ink/80">{s.d}</p>
          </div>
        ))}
      </div>
    </section>
  )
}

function Footer() {
  return (
    <footer className="border-t-2 border-ink/10 py-8 text-center text-sm text-ink-faint">
      <div className="flex flex-wrap items-center justify-center gap-4">
        <a href={DOCS_URL} className="underline decoration-pink decoration-2 underline-offset-4 hover:text-pink">
          源码
        </a>
        <span>·</span>
        <a href={RELEASES_URL} className="underline decoration-pink decoration-2 underline-offset-4 hover:text-pink">
          下载
        </a>
        <span>·</span>
        <span>萤狸科技 × Datawhale · 为爱发电 · 开源共建</span>
      </div>
    </footer>
  )
}

export default function App() {
  const [page, setPage] = useState('home')   // 'home' | 'tool'

  return (
    <div className="min-h-screen bg-cream">
      {/* 顶部导航 */}
      <header className="sticky top-0 z-10 border-b-2 border-ink bg-cream/90 backdrop-blur">
        <div className="mx-auto flex max-w-5xl items-center justify-between px-6 py-3">
          <button onClick={() => setPage('home')} className="flex items-center gap-2 font-black">
            <span className="inline-block h-4 w-4 rounded-full bg-pink shadow-brutal-xs" />
            VibeKey
          </button>
          <nav className="flex items-center gap-3">
            <button
              onClick={() => setPage('home')}
              className={`text-sm font-semibold ${page === 'home' ? 'text-pink' : 'text-ink-muted hover:text-ink'}`}
            >
              首页
            </button>
            <button
              onClick={() => setPage('tool')}
              className={`text-sm font-semibold ${page === 'tool' ? 'text-pink' : 'text-ink-muted hover:text-ink'}`}
            >
              配置工具
            </button>
            <a href={DOWNLOAD_URL} className="btn-brutal btn-brutal-secondary px-3 py-1.5 text-sm">
              下载
            </a>
          </nav>
        </div>
      </header>

      {page === 'home' ? (
        <>
          <Hero onOpenTool={() => setPage('tool')} />
          <Features />
          <HowItWorks />
        </>
      ) : (
        <ConfigTool onBack={() => setPage('home')} />
      )}

      <Footer />
    </div>
  )
}
