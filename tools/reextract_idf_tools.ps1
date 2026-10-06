# 重新解压 eim 装坏的 ESP-IDF 工具链
#
# 背景：eim 把 xtensa-esp-elf 工具链解压坏了——include / libexec / share / picolibc /
# xtensa-esp-elf 都变成了 0 字节文件而不是目录，导致 gcc 找不到 cc1，
# 报 "cannot execute 'cc1'"。压缩包本身是好的（大小与 tools.json 一致），
# 所以不用重新下载，直接重解压即可。
#
# 用 .NET 的 ZipFile.ExtractToDirectory 而不是 Expand-Archive：
# 后者对大 zip 慢且容易在长路径上出错。

$ErrorActionPreference = 'Stop'

$dist = 'C:\Espressif\dist'
$jobs = @(
    @{
        Zip      = 'xtensa-esp-elf-14.2.0_20241119-x86_64-w64-mingw32.zip'
        Dest     = 'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119'
        Expected = 386316009
    },
    @{
        Zip      = 'cmake-3.30.2-windows-x86_64.zip'
        Dest     = 'C:\Espressif\tools\cmake\3.30.2'
        Expected = 45404613
    },
    @{
        Zip      = 'xtensa-esp-elf-gdb-16.2_20250324-x86_64-w64-mingw32.zip'
        Dest     = 'C:\Espressif\tools\xtensa-esp-elf-gdb\16.2_20250324'
        Expected = $null
    }
)

Add-Type -AssemblyName System.IO.Compression.FileSystem

foreach ($job in $jobs) {
    $zipPath = Join-Path $dist $job.Zip
    $dest = $job.Dest

    if (-not (Test-Path $zipPath)) {
        Write-Host "跳过（压缩包不存在）：$zipPath" -ForegroundColor Yellow
        continue
    }

    $size = (Get-Item $zipPath).Length
    if ($job.Expected -and $size -ne $job.Expected) {
        Write-Host "跳过（大小不符，期望 $($job.Expected) 实际 $size）：$zipPath" -ForegroundColor Yellow
        continue
    }

    Write-Host "重解压 $($job.Zip) -> $dest"

    # 先清掉坏的解压结果（0 字节的假目录也在这一步被删掉）
    if (Test-Path $dest) {
        Get-ChildItem -LiteralPath $dest -Force | ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null

    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $dest)
    Write-Host "  完成，条目已展开"
}

Write-Host ''
Write-Host '验证 cc1 是否存在：'
$cc1 = 'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\libexec\gcc\xtensa-esp-elf\14.2.0\cc1.exe'
if (Test-Path $cc1) {
    Write-Host "  OK $cc1" -ForegroundColor Green
} else {
    Write-Host "  仍未找到 $cc1" -ForegroundColor Red
}
