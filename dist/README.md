# VibeKey 分发产物（dist/）

构建时间：2026-10-12

| 文件 | 大小 | SHA256 | 说明 |
|---|---|---|---|
| VibeKey-Setup-0.1.0.exe | 70305774 | `5894faf8347c89029dad5db7020b757dd73da9be2d31cbd81c7d543f5cea733d` | Windows 上位机安装包 |
| vibekey.bin | 1034304 | `02a1af79e8ac21bcf16440877ea9699d2f4b445efef090a1e3894120e29d0851` | ESP32-S3 应用固件（0x10000） |
| bootloader.bin | 21088 | `3586aabda4483be365b161bceb54c3540dae17cd0b9ba5cb95fb5e6160b60a38` | ESP32-S3 引导程序（0x0） |
| partition-table.bin | 3072 | `49d3f472db14dbeefe0bb5723c84a92d98f88a2d44e07dc03a89f5ad4732612e` | ESP32-S3 分区表（0x8000） |

## 安装

### Windows 上位机
双击 VibeKey-Setup-0.1.0.exe，跟随 7 页安装向导。

### ESP32-S3 固件
用 esptool 或上位机的"烧录固件"按钮烧录。

上板前必读 docs/02-上板自检清单.md。