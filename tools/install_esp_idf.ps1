$ErrorActionPreference = 'Stop'

$eim = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\Espressif.EIM-CLI_Microsoft.Winget.Source_8wekyb3d8bbwe\eim.exe'
if (-not (Test-Path $eim)) {
    $found = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages') -Filter 'eim.exe' -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found) { Write-Host '找不到 eim.exe' -ForegroundColor Red; exit 1 }
    $eim = $found.FullName
}

Write-Host "使用 eim: $eim"
Write-Host ''
Write-Host '开始安装 ESP-IDF v5.5（目标 esp32s3）…'
Write-Host '不走镜像（dl.espressif.com 的镜像不托管 git 仓库，且部分资产 404）'
Write-Host 'GitHub 直连可用；Python 包走清华 PyPI 镜像'
Write-Host '（下载约 1GB+，耗时较长，请耐心等待）'
Write-Host ''

# --do-not-track 是全局选项，必须放在子命令之前
& $eim --do-not-track true install `
    --idf-versions 'v5.5' `
    --target 'esp32s3' `
    --path "$env:USERPROFILE\.espressif" `
    --non-interactive true `
    --pypi-mirror 'https://pypi.tuna.tsinghua.edu.cn/simple' `
    --verbose

$code = $LASTEXITCODE
Write-Host ''
Write-Host "eim 退出码: $code"
exit $code
