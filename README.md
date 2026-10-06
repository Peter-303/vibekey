# VibeKey

Vibe Coding 专用键盘的软件开发仓库（萤狸科技 × Datawhale，硬件与固件全开源）。

硬件：ESP32-S3-WROOM-1，**7 个机械按键 + 1 个可按压旋转编码器 = 10 个输入事件**，
蓝/红两颗 LED，MEMS 麦克风，USB-C。

## 文档（先读这里）

| 文档 | 内容 |
|---|---|
| [docs/00-技术设计文档.md](docs/00-技术设计文档.md) | **M0 协议冻结文档**。帧格式、字节序、CRC、按键字符表、状态机、7 处 PRD 差异裁决、ADR |
| [docs/01-引脚分配表.md](docs/01-引脚分配表.md) | 待硬件组填写的 GPIO 分配表 |
| [docs/02-上板自检清单.md](docs/02-上板自检清单.md) | 拿到板子后的逐项验证步骤，含异常对照表 |
| [firmware/README.md](firmware/README.md) | 固件分层架构与测试说明 |
| [host/README.md](host/README.md) | 上位机结构、构建方式与本机环境坑 |

## 结构

```
VibeKey/
├── docs/          技术设计文档（协议在这里冻结）
├── firmware/      下位机 ESP32-S3 固件（C / ESP-IDF）
│   ├── main/app/     ★ 应用层：纯 C99，零 ESP-IDF 依赖，可在 PC 上单测
│   ├── main/port/    ★ 移植层：唯一硬件接触面，硬件变动只改这里
│   └── test/         主机端测试，不需要硬件和 ESP-IDF
├── host/          上位机 Windows 应用（C# / .NET 8 / WPF）
│   ├── src/VibeKey.Protocol/   帧编解码，与固件 proto.c 逐字节一致
│   ├── src/VibeKey.App/        WPF 应用
│   └── tests/                  xUnit 测试
└── tools/         校验与一键测试脚本
```

## 三条最重要的约束

1. **应用层与移植层彻底分离。** `app/` 下不得 `#include` 任何 ESP-IDF 头文件，
   所有外部能力通过 `const vk_port_t *` 函数指针表获取。用户已明确
   "移植层后面会有变动"，所以换硬件只改 `main/port/`。
2. **协议在 `docs/00-技术设计文档.md` 冻结。** 任何字段变更必须同时修改
   设计文档、`firmware/main/app/proto.c`、`host/.../FrameCodec.cs` 和两边测试向量，
   不允许单边修改。
3. **同一事件在 USB/WiFi 双通道上共用同一个 SEQ**，否则上位机去重会误删真实事件。

## 快速开始

```bash
# 不需要硬件、不需要 ESP-IDF，验证固件应用层（70502 项断言）
powershell -ExecutionPolicy Bypass -File tools/run_c_tests.ps1

DN="/c/Program Files/dotnet/dotnet.exe"   # dotnet 不在 PATH 上，写全路径
DOTNET_ROOT="/c/Program Files/dotnet"

# 验证上位机协议与 C 侧逐字节一致（共 47 项测试）
cd host && "$DN" test VibeKey.sln

# 启动上位机界面（默认走模拟设备，不需要插硬件）
"$DN" run --project host/src/VibeKey.App

# 构建 ESP32-S3 固件（ESP-IDF v5.5，本机已装好）
powershell -ExecutionPolicy Bypass -File tools/idf.ps1 build

# 一键构建安装包（Inno Setup 7 页向导，产物在 installer/output/）
powershell -ExecutionPolicy Bypass -File tools/build_installer.ps1

```

## 进度

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 技术设计文档 + 引脚表 + 双端工程骨架 | ✅ 完成 |
| M1 | LED 状态机 + UART1 上报 + 按键消抖 + USB CDC 枚举 | 🔄 应用层测通 + USB CDC 已实现并入固件；WiFi/ASR 待接，未上板联调 |
| M2 | 配网闭环（AP/STA + NVS + 上位机配网页） | ✅ 软件闭环完整（固件 WiFi + UDP 发现 + 配网流程与 UI）；未上板联调 |
| M3 | 心跳 + 自由映射 + 烧录入口 + 开机自启 | ✅ 软件完整（含 Flasher + 端口跳变 + 托盘 + 注册表自启）；未上板联调 |
| M4 | 语音闭环（流式 + 整包 ASR） | 🔄 I2S 采集 + 腾讯云 WebSocket 已实现；整包缓存与真实凭据联调待做 |
| M5 | 一键安装包 + 干净虚拟机验证 | ✅ 软件完整（Inno Setup 7 页向导 + 无驱动 + 已装/启/卸全链路验证）；干净虚拟机实测待做 |

## 测试状态（双端都实测通过）

| 端 | 命令 | 结果 |
|---|---|---|
| 固件应用层 | `powershell -ExecutionPolicy Bypass -File tools/run_c_tests.ps1` | **70502 项断言全部通过**，`-Wall -Wextra -Wpedantic` 零警告 |
| 上位机（全功能） | `cd host && dotnet test` | **68 项 xUnit 测试全部通过**（协议 30 + 传输/跳变 17 + 发现 10 + 配网 5 + 自启 6） |
| 上位机界面 | `dotnet run --project host/src/VibeKey.App` | 正常启动，主窗口渲染无异常 |
| ESP32-S3 固件 | `powershell -File tools/idf.ps1 build` | **从零构建通过，1103/1103 目标编译，vibekey.bin 1034185 字节（app 分区 2MB，余量 51%）** |

两端用**同一组测试向量**（docs/00-技术设计文档.md §3.3 的三组帧），
这是"C 与 C# 协议实现逐字节一致"的唯一裁判。
