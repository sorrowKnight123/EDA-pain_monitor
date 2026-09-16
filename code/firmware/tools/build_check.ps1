# 全工程编译 + 链接检查：直接调用 STM32CubeIDE 自带的 GNU Arm 工具链
# 编译 Core 与 HAL 全部源文件后按工程的链接脚本链接，验证无语法错误、
# 无缺失/重复符号（新增模块接错、声明与定义不一致会在此暴露）。
# 用法：pwsh -File tools/build_check.ps1
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$tcRoot = "E:\STM32CubeIDE\STM32CubeIDE\plugins"
$binDir = Get-ChildItem -Path $tcRoot -Directory -ErrorAction SilentlyContinue |
       Where-Object { $_.Name -like "*gnu-tools-for-stm32*" } |
       ForEach-Object { Join-Path $_.FullName "tools\bin" } |
       Where-Object { Test-Path (Join-Path $_ "arm-none-eabi-gcc.exe") } | Select-Object -First 1
if (-not $binDir) { Write-Error "未找到 arm-none-eabi-gcc（请检查 STM32CubeIDE 安装路径）"; exit 1 }
$gcc = Join-Path $binDir "arm-none-eabi-gcc.exe"
Write-Host "toolchain: $gcc"

$out = if ($env:BUILD_CHECK_OUT) { Join-Path $root $env:BUILD_CHECK_OUT } else { Join-Path $root "build_check" }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out | Out-Null

$inc = @(
  "-I$root\Core\Inc",
  "-I$root\Core\Inc\BSP",
  "-I$root\Drivers\STM32F1xx_HAL_Driver\Inc",
  "-I$root\Drivers\STM32F1xx_HAL_Driver\Inc\Legacy",
  "-I$root\Drivers\CMSIS\Device\ST\STM32F1xx\Include",
  "-I$root\Drivers\CMSIS\Include"
)
$defs = @("-DUSE_HAL_DRIVER", "-DSTM32F103xE", "-DDEBUG")
# 优化等级可用环境变量覆盖（默认 -O0，与 IDE 的 Debug 配置一致；Release 配置为 -Os）
$opt = if ($env:BUILD_CHECK_OPT) { $env:BUILD_CHECK_OPT } else { "-O0" }
$flags = @("-mcpu=cortex-m3", "-std=gnu11", "-g3", $opt, "-Wall",
           "-ffunction-sections", "-fdata-sections", "-mfloat-abi=soft", "-mthumb",
           "--specs=nano.specs")

# 源文件：Core 全部子目录（含 BSP）+ HAL/Src（HAL 全编，与 CubeMX 生成的 Makefile 一致，只多不少）
$srcs = @()
$srcs += Get-ChildItem "$root\Core" -Filter *.c -Recurse | ForEach-Object { $_.FullName }
$srcs += Get-ChildItem "$root\Drivers\STM32F1xx_HAL_Driver\Src" -Filter *.c | ForEach-Object { $_.FullName }
# 启动文件（汇编）单独编译
$startup = Join-Path $root "Core\Startup\startup_stm32f103zetx.s"

$objs = @(); $fail = 0; $warn = 0
foreach ($s in $srcs) {
  $o = Join-Path $out ((Split-Path $s -Leaf) -replace '\.c$', '.o')
  $objs += $o
  $log = & $gcc @flags @defs @inc -c $s -o $o 2>&1
  $diag = $log | Where-Object { $_ -match "error:|warning:" }
  if ($log -match "error:") {
    $fail++
    Write-Host "FAIL $([System.IO.Path]::GetFileName($s))"
    $diag | ForEach-Object { Write-Host "     $_" }
  } elseif ($diag) {
    $warn++
    Write-Host "WARN $([System.IO.Path]::GetFileName($s))"
    $diag | ForEach-Object { Write-Host "     $_" }
  }
}
$oStart = Join-Path $out "startup.o"; $objs += $oStart
$log = & $gcc @flags @defs @inc -c $startup -o $oStart 2>&1
if ($log -match "error:") { $fail++; Write-Host "FAIL startup_stm32f103zetx.s"; $log | ForEach-Object { Write-Host "     $_" } }

if ($fail -gt 0) { Write-Host ""; Write-Host "编译失败，跳过链接"; exit 1 }

# 链接：与工程 Debug/makefile 相同的参数
$elf = Join-Path $out "lcd.elf"
$map = Join-Path $out "lcd.map"
$ld  = Join-Path $root "STM32F103ZETX_FLASH.ld"
$linkArgs = @("-o", $elf) + $objs + @(
  "-mcpu=cortex-m3", "-T$ld", "--specs=nosys.specs",
  "-Wl,-Map=$map", "-Wl,--gc-sections", "-static", "--specs=nano.specs",
  "-mfloat-abi=soft", "-mthumb", "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group")
$link = & $gcc @linkArgs 2>&1
if ($link -match "error|undefined reference|multiple definition") {
  Write-Host "FAIL 链接"
  $link | Where-Object { $_ -match "error|undefined|multiple" } | ForEach-Object { Write-Host "     $_" }
  exit 1
}

$size = & (Join-Path $binDir "arm-none-eabi-size.exe") $elf 2>&1
Write-Host ""
Write-Host "编译文件=$($srcs.Count + 1)  错误文件=$fail  警告文件=$warn"
$size | ForEach-Object { Write-Host $_ }
Write-Host "PASS: 全工程编译 + 链接通过（无缺失/重复符号）"
