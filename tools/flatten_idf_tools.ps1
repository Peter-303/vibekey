# 拍平 eim 装错的工具目录（PowerShell）
#
# 背景：eim（ESP-IDF Installation Manager）解压部分工具压缩包时没有去掉压缩包内的
# 顶层目录，导致布局变成 <工具>/<版本>/<包名>/xxx.exe，而 ESP-IDF 的 idf_tools.py
# 期望可执行文件直接位于 <工具>/<版本>/ 下。结果是 export 时报
# "tool xxx has no installed versions"。
#
# 已知受影响：ccache、dfu-util。
# cmake 的 <版本>/bin 与 esp-clang-libs 的嵌套是正常布局，不动。

$ErrorActionPreference = 'Stop'

$flatten = @(
    @{ Path = 'C:\Espressif\tools\ccache\4.10.2\ccache-4.10.2-windows-x86_64' },
    @{ Path = 'C:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64' }
)

foreach ($item in $flatten) {
    $nested = $item.Path
    if (-not (Test-Path $nested)) {
        Write-Host "跳过（不存在）：$nested"
        continue
    }

    $parent = Split-Path $nested -Parent
    Write-Host "拍平 $nested -> $parent"

    Get-ChildItem -Path $nested -Force | ForEach-Object {
        $dest = Join-Path $parent $_.Name
        Move-Item -LiteralPath $_.FullName -Destination $dest -Force
    }

    # 确认搬空后再删空目录
    $remaining = Get-ChildItem -Path $nested -Force -ErrorAction SilentlyContinue
    if (-not $remaining) {
        Remove-Item -LiteralPath $nested -Force
        Write-Host "  已删除空的嵌套目录"
    }
    else {
        Write-Host "  警告：嵌套目录仍有 $($remaining.Count) 个条目，未删除" -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host '完成。现在可以通过 tools\idf.ps1 验证：'
Write-Host '  powershell -ExecutionPolicy Bypass -File tools\idf.ps1 idf.py --version'
