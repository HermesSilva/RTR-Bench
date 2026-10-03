# RTR-Bench - builds on Windows with clang (LLVM) and Ninja into build\.
#
#   -Clean    removes build\ first
#   -Debug    debug build (default: Release)
#   -NoTidy   skips clang-tidy
param(
    [switch]$Clean,
    [switch]$Debug,
    [switch]$NoTidy
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$llvm = 'C:/Program Files/LLVM/bin'   # forward slashes: CMake reads these as strings

if ($Clean -and (Test-Path $build)) {
    [System.IO.Directory]::Delete($build, $true)
}

# clang on Windows uses the MSVC C++ library and the Windows SDK: import the
# Visual Studio environment (INCLUDE, LIB, PATH) so that the linker finds them.
if (-not $env:VCToolsInstallDir) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) {
        Write-Error "Visual Studio Build Tools with C++ not found: install them (the C++ library and Windows SDK come from there)."
    }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

$type = if ($Debug) { 'Debug' } else { 'Release' }
$tidy = if ($NoTidy) { 'OFF' } else { 'ON' }

cmake -S $root -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$type" `
    "-DCMAKE_C_COMPILER=$llvm/clang.exe" `
    "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" `
    "-DCMAKE_LINKER=$llvm/lld-link.exe" `
    "-DCMAKE_RC_COMPILER=$llvm/llvm-rc.exe" `
    "-DCLANG_TIDY_EXE=$llvm/clang-tidy.exe" `
    "-DRTR_BENCH_TIDY=$tidy"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $build
exit $LASTEXITCODE
