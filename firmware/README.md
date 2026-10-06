# VibeKey 固件（下位机 · ESP32-S3）

技术设计文档：[../docs/00-技术设计文档.md](../docs/00-技术设计文档.md)
引脚分配表：[../docs/01-引脚分配表.md](../docs/01-引脚分配表.md)

## 分层架构

固件严格分为两层，**这是本项目最重要的代码约束**：

```
main/
├── app/    应用层 —— 纯 C99，零 ESP-IDF 依赖，可在 PC 上编译和单测
└── port/   移植层 —— 唯一的硬件接触面，硬件变动只改这里
```

| 目录 | 内容 | 能否在 PC 上跑 |
|---|---|---|
| `main/app/` | proto / status / key_fsm / led_fsm / seq / net_fsm / asr_fsm / cmd_dispatch / app | ✅ 全部可以 |
| `main/port/` | `port_board.h`（★引脚表）、`port.h`（接口）、`port_esp32.c`（ESP-IDF 实现） | ❌ 需要 ESP-IDF |

**规则**：`app/` 下任何 `.c` 文件不得 `#include` ESP-IDF 头文件。所有外部能力
都通过 `const vk_port_t *` 函数指针表获取（见 `app/port.h`）。换硬件 = 新增一个
`port_xxx.c` + 改 `port_board.h`，`app/` 一行不改。

## 当前进度

| 模块 | 状态 |
|---|---|
| `app/proto.c` 帧编解码 | ✅ 完成，含主机测试 |
| `app/status.c` 状态码与上报字符串 | ✅ 完成，含主机测试 |
| `app/key_fsm.c` 按键/编码器消抖 | ✅ 完成，含主机测试 |
| `app/led_fsm.c` LED 状态机 | ✅ 完成，含主机测试 |
| `app/seq.c` 序号分配器 | ✅ 完成，含主机测试 |
| `app/cmd_dispatch.c` 下行分发 + 烧录入口校验 | ✅ 完成，含主机测试 |
| `app/net_fsm.c` 配网状态机 | ✅ 完成，含主机测试 |
| `app/asr_fsm.c` 语音会话状态机 | ✅ 完成，含主机测试 |
| `app/app.c` 主编排 | ✅ 完成，含集成测试 |
| `app/port.h` 移植层接口 | ✅ 已冻结 |
| `port/port_board.h` 引脚表 | 🟡 占位宏，待硬件组 |
| `port/port_esp32.c` ESP-IDF 实现 | 🔄 GPIO/UART1/LED/时间/**USB CDC**/**WiFi AP+STA+TCP+UDP发现**/**I2S 麦克风**/**腾讯云 ASR(WebSocket)** 已实现并入固件；整包缓存待做 |
| `main.c` + ESP-IDF 工程文件 | ✅ 骨架就绪（**未经编译**，开发机无 ESP-IDF） |

## 不需要硬件就能跑测试

```bash
# Linux / macOS
cd test && make run

# Windows（自动找 gcc，不用手动配 PATH）
powershell -ExecutionPolicy Bypass -File ..\..\tools\run_c_tests.ps1
```

**当前状态：70497 项断言全部通过，`-Wall -Wextra -Wpedantic` 零警告**
（用 MinGW-W64 GCC 16.2.0 实测，见 firmware/test/Makefile）

如果没有 gcc，装一个（都不需要管理员）：

```bash
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --scope user
```

测试覆盖（全部对应 PRD 验收条款）：

| 测试文件 | 覆盖 |
|---|---|
| `test_proto.c` | 三组文档向量逐字节比对、编解码往返、坏帧重同步、流式解码 |
| `test_status.c` | 状态字符串格式、UART1 行、截断安全 |
| `test_key_fsm.c` | 15ms 消抖阈值、一次一事件、长按不连发、编码器方向与档位 |
| `test_led_fsm.c` | PRD 5.1 五档状态、红蓝交替互斥 |
| `test_seq.c` | 单调递增、16 位回绕 |
| `test_cmd_dispatch.c` | **烧录入口四道校验**、重放窗口、WiFi/ASR 配置解析 |
| `test_net_fsm.c` | PRD 5.2 四条验收：空 NVS 进 AP、错密码回退、拔路由不死机、新配置 |
| `test_asr_fsm.c` | 流式/整包、**ASR 失败不降级为故障**、麦克风故障进 ERROR |
| `test_app.c` | 集成：启动配网、按键上行、DFU 通道规则、语音键、心跳、掉线回退 |

## 测试抓到的真实 bug

这些是写代码时没发现、靠编译+跑测试才暴露的，已全部修复：

| # | 问题 | 后果 |
|---|---|---|
| 1 | `vk_cmd_init` 的 `memset` 把先设置的 `voice_key` 清成了 0 | **语音键完全不触发**，语音功能静默失效 |
| 2 | `vk_asr_event()` 的返回值（该调哪个 port 函数）在按键和指令路径里被丢弃 | 麦克风永远不启动 |
| 3 | `net_fsm` 超时后没重置窗口起点 | 3 次重试在同一毫秒内全部耗尽，回退 AP 过快 |
| 4 | `proto_stream_feed` 在输出数组满时会吞帧 | 高频按键丢事件 |
| 5 | `STATUS_REQ` 只置脏标志，上报要等下一次 poll | 上位机明确请求状态时收不到立即响应 |
| 6 | 网络信息（IP/RSSI）没同步进 `status` | 上位机状态卡显示空 IP |
| 7 | ASR 在 IDLE 态失败不上告 | 无外网时用户看不到"语音不可用" |

## 上板前的自检

`port_board.h` 的 GPIO 目前是**占位值**。拿到板子后先按
[../docs/02-上板自检清单.md](../docs/02-上板自检清单.md) 逐项验证：
第 0 步测出真实接线，第 4 步验证按键字符与旋钮方向。
旋钮方向接反了只需改 `port_board.h`，不用动 `app/` 的代码。

## ESP-IDF 构建（本机已装好 v5.5）

```bash
# 一条命令构建（内部自动激活环境 + 指定 ASCII 构建目录）
powershell -ExecutionPolicy Bypass -File ..	ools\idf.ps1 build

# 烧录与监视
powershell -ExecutionPolicy Bypass -File ..\tools\idf.ps1 -p COM12 flash monitor
```

**当前状态：从零构建通过，1103/1103 目标全部编译，
`vibekey.bin` 1034185 字节，占 app 分区 49%（51% 余量）。**
（app 分区已从 1MB 扩到 2MB——加了 WebSocket + mbedTLS 后 1MB 只剩 1% 余量。
flash 是 16MB，余量充足，改分区表比裁功能更干净）

### 为什么构建目录必须是 ASCII 路径

项目本身在 `D:\键盘配套硬件开发\` 下（含中文）。ESP-IDF 在 Windows 上的构建链是

```
ninja -> cmd.exe /C "cd /D <项目路径>/build && objdump <项目路径>/build/.../libxtensa.a"
```

中文路径会被 cmd.exe 转错（表现为 `D:/Ӳ/VibeKey/...: No such file or directory`），
链接阶段的 ldgen 必然失败。

试过但**无效**的办法：
- `chcp 65001` —— 子进程仍然转错
- 目录联接 `D:\VibeKey -> 项目目录` —— CMake 会解析到真实路径，联接被穿透

**有效**的办法：构建目录放纯 ASCII 路径（tools/idf.ps1 已默认用 `D:/vkbuild`，可用环境变量 `VK_IDF_BUILD_DIR` 改）。源码路径含中文没关系——gcc 能处理，实测 700+ 个对象编译通过；只有"构建产物路径"必须是 ASCII。

### USB CDC（已实现）

数据通道走 TinyUSB CDC，VID/PID 按设计文档 §7.1 冻结为 **303A:4001**。

组件管理器引入的 `espressif/tinyusb` **只提供裸 API**——原先 ESP-IDF 内置的
`tinyusb_cdcacm_init()` 封装已随内置组件一起移除，所以这里直接调
`tud_cdc_n_*()` / `tusb_rhport_init()`，并自己实现三个描述符回调
（`tud_descriptor_device_cb` / `configuration_cb` / `string_cb`）。

TinyUSB 协议栈跑在**独立任务**里，不在主循环中：`tud_task()` 需要高频调用，
而主循环有 5ms 按键轮询节拍。USB 任务里 `tud_task()` 与上行读泵必须紧邻调用，
分开会漏掉刚到的字节。

### 本机 ESP-IDF 的几个坑（都已处理，见 tools\idf.ps1 注释）

| 现象 | 原因 | 处理 |
|---|---|---|
| `idf.py` 只打印一句 MSys 警告就退出、退出码还是 0 | `MSYSTEM` 变量存在时 idf.py 不调用 `main()` | PowerShell 里 `Remove-Item Env:MSYSTEM` |
| `tool xxx has no installed versions` | eim 把工具链装在 `C:\Espressif	ools`，而 ESP-IDF 拼的是 `$IDF_TOOLS_PATH	ools\...` | `IDF_TOOLS_PATH=C:\Espressif` |
| `espidf.constraints.v5.5.txt doesn't exist` | eim 漏生成 | 复制到 `C:\Espressif5.5\` |
| `'gbk' codec can't decode byte`（kconfgen 崩） | 中文 Windows 上 Python 默认用 GBK 开文件 | `PYTHONUTF8=1` |
| `cmake must be available on the PATH` | WinLibs 自带的 cmake 4.4.2 在 PATH 里，版本不符导致 ESP-IDF 跳过自带 cmake | 从 PATH 剔除 WinLibs |
| `cannot execute 'cc1'` | eim 解压 xtensa 工具链时把 `include/libexec/share` 建成了 0 字节文件 | 从 `C:\Espressif\dist` 的 zip 重新解压 |
| `tusb_config.h: No such file or directory` | v5.5 起 TinyUSB 改用组件管理器，且需要项目自带配置 | `idf.py add-dependency` + `main/tusb_config.h` |

> ⚠️ `port_board.h` 目前是占位引脚，**上板前必须先填写真实引脚分配表**。
> 普通用户不走这条路，他们用上位机的"烧录固件"按钮（magic word 校验，
> 见设计文档 §7.1）。

## 已冻结的协议参数

详见 [../docs/00-技术设计文档.md](../docs/00-技术设计文档.md) §3。要点：

- 帧：`0xAA | LEN(2B,LE) | TYPE(1B) | SEQ(2B,LE) | PAYLOAD | CRC16(2B,LE)`
- CRC16/CCITT-FALSE（poly 0x1021, init 0xFFFF）
- 按键字符表：`1`–`7`（7 个机械键）、`+`/`-`（编码器左右旋）、`0`（编码器按下）
- 同一事件在 USB/WiFi 双通道上**共用同一个 SEQ**
- 烧录指令仅接受来自 USB 通道，载荷为 8 字节 magic word `"VIBEFLAS"`
