# VibeKey 上位机（Windows / C# / .NET 8 / WPF）

技术设计文档：[../../docs/00-技术设计文档.md](../../docs/00-技术设计文档.md)

## 方案

| 项 | 选择 | 理由 |
|---|---|---|
| 语言/框架 | **C# / .NET 10 / WPF** | SerialPort、注册表、托盘、设备通知全是内置 API；可自包含单文件发布；不需要 Node/Rust 工具链 |
| 架构 | MVVM，手写 `INotifyPropertyChanged` 基类 | 不引第三方包，减少 NuGet 还原失败面 |
| 通道抽象 | `IDeviceTransport` | USB CDC / WiFi TCP / **模拟设备** 走同一条上层代码路径 |
| 烧录 | C# 原生实现 ESP32 ROM 串行协议 | 不打包 Python 运行时（ADR-3） |
| 打包 | Inno Setup（后续补） | 7 页安装向导（PRD 6.6） |

## 结构

```
host/
├── VibeKey.sln
├── src/
│   ├── VibeKey.Protocol/      ★ 帧编解码，与固件 proto.c 逐字节一致
│   │   └── FrameCodec.cs
│   └── VibeKey.App/
│       ├── Services/
│       │   ├── IDeviceTransport.cs    通道抽象
│       │   ├── SimulatedTransport.cs  ★ 模拟设备，无硬件也能开发 UI
│       │   ├── DeviceSession.cs       心跳 / SEQ 去重 / 状态缓存
│       │   ├── MappingStore.cs        %AppData%/VibeKey/mappings.json
│       │   └── ActionExecutor.cs      五类动作的校验与执行
│       ├── Models/KeyMapping.cs
│       ├── ViewModels/MainViewModel.cs
│       ├── Views/MainWindow.xaml
│       └── App.xaml
└── tests/
    └── VibeKey.Protocol.Tests/  ★ 与固件 test_proto.c 共用同一组向量
```

## 构建与测试

```bash
cd host
dotnet build
dotnet test                       # 协议编解码测试（与 C 侧同一组向量）
dotnet run --project src/VibeKey.App
```

**当前状态：`dotnet build` 0 警告 0 错误；`dotnet test` 30 项全部通过；
应用可正常启动（实测主窗口标题 "VibeKey 控制台"）。**

### 本机环境（2026-10-05 核实）

机器上装的是 **.NET 10.0.401 SDK**（`C:\Program Files\dotnet\`，含 WPF 的
WindowsDesktop 运行时），因此四个项目的目标框架都是 `net10.0` / `net10.0-windows`。
但 `dotnet` **不在 PATH 上**，调用时要写全路径：

```bash
DN="/c/Program Files/dotnet/dotnet.exe"
DOTNET_ROOT="/c/Program Files/dotnet" "$DN" build VibeKey.sln
DOTNET_ROOT="/c/Program Files/dotnet" "$DN" test VibeKey.sln
```

> 早期曾为调试在 `%USERPROFILE%\dotnet\` 装过一份 .NET 8 SDK，现已重定向到 net10.0，
> 那份不再需要（也不在 PATH 上，可删）。

### NuGet 必须走国内镜像

本机网络把 nuget.org 302 到国内镜像，而 NuGet 客户端不一定跟随，导致 NU1100。
仓库内已放 [nuget.config](nuget.config)，只用 `nuget.azure.cn`。
**不要加回 nuget.org**——实测 25MB+ 的运行时包从 api.nuget.org 下载会断流，
并损坏 NuGet 缓存（`response ended prematurely` / `Central Directory corrupt`）。

## 关键设计

**1. 协议双端一致。** `FrameCodec.cs` 与 `firmware/main/app/proto.c` 必须逐字节一致。
两边用**同一组测试向量**（`docs/00-技术设计文档.md` §3.3 的三组帧），
任何字段变更必须同时改：设计文档、`proto.h`、`proto.c`、`FrameCodec.cs`、两边测试。
不允许单边修改。

**2. 模拟设备优先。** `SimulatedTransport` 按 PRD 5.1 的状态表产生 LED/状态变化，
并对下行指令给出与真实固件一致的反应（包括拒绝错误 magic word）。
这让 UI、映射逻辑、烧录流程可以在**没有任何硬件**的情况下完整开发和自测
（PRD 9 章"上位机提供模拟设备调试模式"的落地）。

**3. 心跳与去重。** 每 1s 发 ping，500ms 内回 pong，连续 3 次超时判离线（PRD 6.2）。
上行 SEQ 由设备分配，上位机跨通道去重（设计文档 §3.4）。

**4. 烧录入口。** 只有点击"烧录固件"并二次确认后才下发 magic word；
且**仅 USB 通道**会被固件接受，WiFi 来的直接拒绝（设计文档 §7.1）。

**5. 烧录时的 COM 口跳变（硬件已确认为情形 A）。** 工作态设备是 TinyUSB CDC
（VID `303A` / PID `4001`）；下发 magic word 后设备重启进下载模式，ROM 启用
USB Serial/JTAG（VID `303A` / PID `1001`），呈现为一个**新的 COM 口**。
同一个 USB-C 口、线不用拔，但口名会变。

`PortJumpWatcher` 负责"何时算找到新口"：记录本轮开始前就已存在的下载态口，
只认本轮真正新出现的那个，避免误认上一轮遗留的口；15s 超时。
`UsbCdcTransport` 负责枚举与串口操作，两者分离以便单测。

## 当前状态

| 模块 | 状态 |
|---|---|
| `VibeKey.Protocol` 帧编解码 + 流式解码 + WiFi 配置解析 | ✅ 已写，30 项测试通过 |
| `SimulatedTransport` | ✅ 已写 |
| `DeviceSession` 心跳/去重/状态 | ✅ 已写 |
| `MappingStore` 持久化 + 导入导出 | ✅ 已写 |
| `ActionExecutor` 五类动作 | ✅ 已写 |
| `MainViewModel` + WPF 界面 | ✅ 骨架可用 |
| `UsbCdcTransport`（VID/PID 枚举 + 断线自动重连） | ✅ 已写，17 项测试通过 |
| `PortJumpWatcher`（烧录时 COM 口跳变侦测） | ✅ 已写，测试通过 |
| `Flasher`（ESP32 ROM 串行协议） | 🟡 待写 |
| `AutoStartService`（HKCU Run + 托盘） | 🟡 待写 |
| 安装包（Inno Setup） | 🟡 待写 |

## UI 约定

- 现代化桌面 UI：圆角卡片、浅色为主、克制的配色（**避免大面积紫色/靛蓝**）
- 字体用系统默认无衬线（微软雅黑 / Segoe UI）
- 无设备连接时显示空状态引导："请用 USB 连接 Vibe Coding 键盘"
- 自启时最小化到托盘，左键展状态、右键退出
