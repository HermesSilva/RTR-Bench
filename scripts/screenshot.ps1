# RTR-Bench - writes a PNG of the rack, with alpha, for the README and the docs.
#
#   scripts\screenshot.ps1 -Out docs\screenshots\rack-dark.png [-Delay 6]
#
# Runs the bench in its screenshot mode: it comes up, connects to the probe,
# and after the delay saves its own framebuffer (so the transparent margins
# stay transparent, whatever is behind the window) and quits. The emulator
# should be running with the probe for the ports to show activity.
param(
    [string]$Out = "docs\screenshots\rack-dark.png",
    [double]$Delay = 6
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\rtr-bench.exe'
if (-not (Test-Path $exe)) {
    Write-Error "Not built yet: run scripts\build.ps1 first."
}
if (-not [System.IO.Path]::IsPathRooted($Out)) {
    $Out = Join-Path $root $Out
}
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

# A GUI executable returns at once: wait for it explicitly.
$p = Start-Process -FilePath $exe -ArgumentList @('--screenshot', "`"$Out`"", '--delay', "$Delay") -PassThru -Wait
if ($p.ExitCode -ne 0 -or -not (Test-Path $Out)) {
    Write-Error "Screenshot failed (exit $($p.ExitCode))."
}
Write-Output $Out
