# 在 ESP-IDF v5.5 环境里执行命令（PowerShell）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\idf.ps1 idf.py --version
#   powershell -ExecutionPolicy Bypass -File tools\idf.ps1 idf.py set-target esp32s3
#   powershell -ExecutionPolicy Bypass -File tools\idf.ps1 idf.py build
#
# ===== 为什么必须经过本脚本 =====
#
# 1) MSYS 变量会让 idf.py 静默退出。
#    tools/idf.py 开头是 if/elif/else：MSYSTEM 存在时只打印一句警告，不调用 main()，
#    于是什么都不做、退出码还是 0，极易误判成"命令成功但没输出"。
#    本机的 Bash 工具就是 MSYS/Mingw 环境，且在里面 unset MSYSTEM 也不会传给子进程
#    （shell 包装层会重新注入），所以必须在 PowerShell 里 Remove-Item Env:MSYSTEM。
#
# 2) 不需要 export.ps1。
#    idf.py 内部会自己调 idf_tools.py export 配好工具链 PATH；手动 source export.ps1
#    反而会因为裸 `python` 解析到错误解释器而引出更多问题。直接指定下面的环境变量即可。
#
# 3) eim 的安装路径与 ESP-IDF 默认假设不一致。
#    eim 把 IDF 装在 %USERPROFILE%\.espressif\v5.5\esp-idf，
#    把工具链和 Python venv 装在 C:\Espressif\tools\...。
#    而 idf_tools.py 拼路径用的是 $IDF_TOOLS_PATH\tools\<名>\<版本>，
#    所以 IDF_TOOLS_PATH 要指到 C:\Espressif（不是 C:\Espressif\tools）。
#
# 4) eim 漏生成 espidf.constraints.v5.5.txt，activate_venv.py 会因它缺失而中止。
#
# 5) ccache 4.10.2（乐鑫官方 win64 构建）在这台机器上一跑就崩：
#    std::filesystem 抛 "Cannot convert character sequence"（中文用户名 + 非 UTF-8
#    区域设置下的字符转换缺陷）。它是可选的增量构建加速器，不影响编译结果，
#    因此关掉它并放宽工具检查。riscv32-esp-elf-gdb 缺失也无所谓——
#    本项目目标是 esp32s3（Xtensa），用不到 RISC-V 的 gdb。
#
# 本脚本故意不写 param() 块——写了会让后续参数被当作脚本自身参数绑定，
# 连 `--version` 都会报"找不到匹配的参数"。没有 param 块时参数自动进 $args。

$ErrorActionPreference = 'Stop'

# 把控制台代码页设为 UTF-8。
# 构建过程中 ninja 会 spawn cmd.exe /C "cd /D <项目路径> && objdump <项目路径>/libxtensa.a"。
# 在默认代码页（中文 Windows 是 936/GBK）下，这条命令里的中文路径会被转错，
# 表现为 "D:/Ӳ/VibeKey/...: No such file or directory"。
# 设成 65001 后子进程继承，路径就能正确传递。
chcp.com 65001 > $null

$idfPath  = 'C:\Espressif\v5.5\esp-idf'
$venvPath = 'C:\Espressif\tools\python\v5.5\venv'
$python   = Join-Path $venvPath 'Scripts\python.exe'
$idfPy    = Join-Path $idfPath 'tools\idf.py'

foreach ($p in @($idfPath, $python, $idfPy)) {
    if (-not (Test-Path $p)) {
        Write-Host "找不到 $p" -ForegroundColor Red
        Write-Host '请先运行：powershell -ExecutionPolicy Bypass -File tools\install_esp_idf.ps1'
        exit 1
    }
}

# 1. 清掉 MSYS 痕迹，否则 idf.py 静默退出。
#    同时把无关的编译器目录从 PATH 剔除——这一步很关键：
#    WinLibs 的 bin 里也带一份 cmake 4.4.2 和 ninja 1.13.2，idf_tools.py 发现
#    "PATH 里有不受支持版本的 cmake" 后会**跳过添加自带的 cmake**，导致
#    idf.py 报 "cmake must be available on the PATH"。
#    （WinLibs 的路径结尾带反斜杠，所以 -like 要写成 '\mingw64\bin*' 这种形式。）
Remove-Item Env:MSYSTEM  -ErrorAction SilentlyContinue
Remove-Item Env:MSYSCON  -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_CHOST -ErrorAction SilentlyContinue
$env:PATH = ($env:PATH -split ';' | Where-Object {
    $_ -and
    $_ -notlike '*\msys64\*' -and
    $_ -notlike '*\mingw64\bin*' -and
    $_ -notlike '*\git\usr\bin*' -and
    $_ -notlike '*WinLibs*' -and
    $_ -notlike '*BrechtSanders*'
}) -join ';'

# 2. 指向 eim 实际安装位置
$env:IDF_TOOLS_PATH     = 'C:\Espressif'   # 注意：不是 C:\Espressif\tools
$env:IDF_PYTHON_ENV_PATH = $venvPath

# 强制 Python 用 UTF-8 读写文件。
# 这台机器是中文 Windows，Python 默认 locale 是 GBK(cp936)。而 ESP-IDF 的
# Kconfig 帮助文本、组件清单等 JSON 都是 UTF-8，一旦含非 ASCII 字符，
# kconfgen 就会崩在 'gbk' codec can't decode byte ...。
# 设了 PYTHONUTF8=1 后 Python 一律按 UTF-8 打开文件，这一类问题全部消失。
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'

# 3. constraints 文件已随 IDF 一起放在 C:\Espressif\v5.5\ 下。
#    另在 C:\Espressif\ 根下补一份，因为 ESP-IDF 有时也会往那里找。
$constraintsSrc = 'C:\Espressif\v5.5\espidf.constraints.v5.5.txt'
if (Test-Path $constraintsSrc) {
    foreach ($c in @(
        'C:\Espressif\espidf.constraints.v5.5.txt',
        (Join-Path $env:USERPROFILE '.espressif\espidf.constraints.v5.5.txt'))) {
        if (-not (Test-Path $c)) {
            Copy-Item $constraintsSrc $c
            Write-Host "已补齐 $c" -ForegroundColor DarkGray
        }
    }
}

# 4. 关掉崩溃的 ccache，放宽工具检查
$env:IDF_CCACHE_ENABLE   = '0'
$env:IDF_SKIP_TOOLS_CHECK = '1'

# 5. 让 idf_tools.py 导出工具链环境（cmake / ninja / xtensa-gcc 等的 PATH）
#    不这么做 idf.py 会报 "cmake must be available on the PATH"。
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$kv = & $python (Join-Path $idfPath 'tools\idf_tools.py') export --format key-value 2>&1
$ErrorActionPreference = $prevEap

$applied = 0
foreach ($line in $kv) {
    if ($line -isnot [string]) { continue }
    if ($line -match '^PATH=(.*)$') {
        $p = $Matches[1]
        if ($p -like '*%PATH%*') { $p = $p -replace '%PATH%', $env:PATH }
        $env:PATH = $p
        $applied++
    }
    elseif ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
        Set-Item ("Env:" + $Matches[1]) $Matches[2]
    }
}
if ($applied -eq 0) {
    Write-Host '警告：未能从 idf_tools.py export 取得 PATH，后续可能缺 cmake/ninja' -ForegroundColor Yellow
}

if ($args.Count -eq 0) {
    Write-Host '用法：tools\idf.ps1 <命令> [参数...]'
    Write-Host '示例：tools\idf.ps1 set-target esp32s3'
    Write-Host '      tools\idf.ps1 build'
    exit 1
}

# 允许两种写法都行：`idf.ps1 idf.py --version` 和 `idf.ps1 --version`。
# 本脚本本身就是 idf.py 的包装，第一个参数若是 idf.py 就剥掉，否则会变成
# `python idf.py idf.py --version` 而报 "No such option"。
if ($args[0] -eq 'idf.py') {
    $args = $args[1..($args.Count - 1)]
}
if ($args.Count -eq 0) {
    Write-Host '用法：tools\idf.ps1 <命令> [参数...]'
    exit 1
}

# ===== 构建目录固定到 ASCII 路径 =====
#
# 为什么必须这样：ESP-IDF 在 Windows 上的构建链是
#   ninja -> cmd.exe /C "cd /D <项目路径>\build && objdump <项目路径>\build\...\libxtensa.a"
# 只要路径里含中文（本项目在 D:\键盘配套硬件开发\ 下），cmd.exe 就会把路径转错，
# 表现为 "D:/Ӳ/VibeKey/...: No such file or directory"，链接阶段的 ldgen 必然失败。
#
# 试过但无效的办法：
#   - chcp 65001（子进程仍转错）
#   - 目录联接 D:\VibeKey -> 项目目录（CMake 会解析到真实路径，联接被穿透）
#
# 有效的办法：把构建目录放到纯 ASCII 路径。源码路径含中文没关系（gcc 能处理，
# 实测 700+ 个对象编译通过），只有"构建产物路径"必须是 ASCII。
#
# 想改到别处：设环境变量 VK_IDF_BUILD_DIR。
if (-not ($args -contains '-B' -or $args -contains '--build-dir')) {
    $buildDir = if ($env:VK_IDF_BUILD_DIR) { $env:VK_IDF_BUILD_DIR } else { 'D:\vkbuild' }
    $args = @('-B', $buildDir) + $args
    Write-Host "构建目录：$buildDir" -ForegroundColor DarkGray
}

Write-Host "[idf $($args -join ' ')]" -ForegroundColor Cyan

# 调试：确认工具链真的在 PATH 上
if ($env:VIBEKEY_IDF_DEBUG) {
    $cm = Get-Command cmake -ErrorAction SilentlyContinue
    Write-Host "[dbg] cmake = $(if ($cm) { $cm.Source } else { 'NOT FOUND' })" -ForegroundColor Magenta
    Write-Host "[dbg] PATH 含 cmake 的条目：" -ForegroundColor Magenta
    ($env:PATH -split ';') | Where-Object { $_ -like '*cmake*' } | ForEach-Object { Write-Host "    [$_]" -ForegroundColor Magenta }
}

& $python $idfPy @args
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host "命令退出码: $code" -ForegroundColor Yellow
}
exit $code
