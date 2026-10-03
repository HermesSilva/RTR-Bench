# RTR-Bench - runs the bench built by build.ps1.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\rtr-bench.exe'
if (-not (Test-Path $exe)) {
    Write-Error "Not built yet: run scripts\build.ps1 first."
}
& $exe @args
exit $LASTEXITCODE
