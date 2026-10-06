# 一键构建 VibeKey 安装包（Inno Setup）
#
# 流程：
#   1. Release 发布 WPF 应用为自包含单文件 exe（不需要用户装 .NET 运行时）
#   2. 用 Inno Setup 编译 7 页安装向导
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\build_installer.ps1
#
# 产物：installer\output\VibeKey-Setup-x.y.z.exe
#
# 前置：Inno Setup 6 已装（winget install -e --id JRSoftware.InnoSetup --scope user）

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$hostDir = Join-Path $root 'host'
$iscc = Join-Path $env:USERPROFILE 'AppData\Local\Programs\Inno Setup 6\ISCC.exe'

if (-not (Test-Path $iscc)) {
    Write-Host "找不到 Inno Setup 编译器：$iscc" -ForegroundColor Red
    Write-Host '请先装：winget install -e --id JRSoftware.InnoSetup --scope user'
    exit 1
}

# dotnet 不在 PATH 上，写全路径
$dotnet = 'C:\Program Files\dotnet\dotnet.exe'
if (-not (Test-Path $dotnet)) {
    Write-Host "找不到 dotnet：$dotnet" -ForegroundColor Red
    exit 1
}

Write-Host '[1/2] 发布 WPF 应用（Release，自包含单文件）…' -ForegroundColor Cyan
Push-Location $hostDir
try {
    & $dotnet publish src/VibeKey.App `
        -c Release -r win-x64 `
        --self-contained true `
        -p:PublishSingleFile=true `
        -p:IncludeNativeLibrariesForSelfExtract=true `
        -p:EnableCompressionInSingleFile=true `
        -o publish/win-x64
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish 失败，退出码 $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$exe = Join-Path $hostDir 'publish\win-x64\VibeKey.exe'
$size = (Get-Item $exe).Length
Write-Host "  产物：$exe（$([math]::Round($size/1MB,1)) MB）"

Write-Host '[2/2] 用 Inno Setup 编译安装包…' -ForegroundColor Cyan
Push-Location (Join-Path $root 'installer')
try {
    & $iscc vibekey.iss
    if ($LASTEXITCODE -ne 0) { throw "ISCC 编译失败，退出码 $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$out = Join-Path $root 'installer\output\VibeKey-Setup-0.1.0.exe'
$outSize = (Get-Item $out).Length
Write-Host ''
Write-Host "安装包已生成：$out" -ForegroundColor Green
Write-Host "大小：$([math]::Round($outSize/1MB,1)) MB"
