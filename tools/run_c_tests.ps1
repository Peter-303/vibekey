# 一键构建并运行固件应用层测试（Windows）
#
# 用途：不需要 ESP-IDF、不需要硬件，验证 app/ 下全部逻辑。
# 用法：在 firmware\test 目录执行
#     powershell -ExecutionPolicy Bypass -File ..\..\tools\run_c_tests.ps1
#
# 会自动在下列位置寻找 gcc：
#   1. PATH 里的 gcc
#   2. winget 安装的 WinLibs
#   3. MSYS2 / MinGW 常见路径

$ErrorActionPreference = 'Stop'

function Find-Gcc {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @()
    $wingetRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $wingetRoot) {
        $candidates += Get-ChildItem -Path $wingetRoot -Filter 'gcc.exe' -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName }
    }
    foreach ($root in @("$env:LOCALAPPDATA\Programs", 'C:\', 'D:\')) {
        foreach ($sub in @('msys64\mingw64\bin\gcc.exe', 'mingw64\bin\gcc.exe', 'MinGW\bin\gcc.exe', 'tools\mingw64\bin\gcc.exe')) {
            $p = Join-Path $root $sub
            if (Test-Path $p) { $candidates += $p }
        }
    }
    if ($candidates.Count -gt 0) { return $candidates[0] }
    return $null
}

$gcc = Find-Gcc
if (-not $gcc) {
    Write-Host '找不到 gcc。请先安装其一：' -ForegroundColor Red
    Write-Host '  winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT'
    Write-Host '  或安装 MSYS2 后执行 pacman -S mingw-w64-ucrt-x86_64-gcc'
    exit 1
}

Write-Host "使用编译器：$gcc" -ForegroundColor Green
$gccDir = Split-Path $gcc -Parent

# gcc 需要在自己的目录下才能找到 cc1 等内部组件
$env:PATH = "$gccDir;$env:PATH"

$appDir = Join-Path $PSScriptRoot '..\firmware\main\app'
$testDir = Join-Path $PSScriptRoot '..\firmware\test'
$out = Join-Path $testDir 'run_tests.exe'

$appSources = @(
    'proto.c', 'status.c', 'key_fsm.c', 'led_fsm.c', 'seq.c',
    'cmd_dispatch.c', 'net_fsm.c', 'asr_fsm.c', 'app.c'
) | ForEach-Object { Join-Path $appDir $_ }

$testSources = @(
    'test_proto.c', 'test_status.c', 'test_key_fsm.c', 'test_led_fsm.c',
    'test_seq.c', 'test_cmd_dispatch.c', 'test_net_fsm.c', 'test_asr_fsm.c',
    'test_app.c', 'test_main.c', 'stub_port.c'
) | ForEach-Object { Join-Path $testDir $_ }

$args = @(
    '-std=c99', '-Wall', '-Wextra', '-Wpedantic', '-O1', '-g',
    "-I$appDir", '-o', $out
) + $appSources + $testSources

Write-Host '编译中…' -ForegroundColor Cyan
& $gcc @args
if ($LASTEXITCODE -ne 0) {
    Write-Host '编译失败' -ForegroundColor Red
    exit 1
}

Write-Host ''
& $out
$code = $LASTEXITCODE
Write-Host ''
if ($code -eq 0) {
    Write-Host '全部通过。' -ForegroundColor Green
} else {
    Write-Host '有测试未通过，见上面的 FAIL 行。' -ForegroundColor Red
}
exit $code
