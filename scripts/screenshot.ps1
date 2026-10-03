# RTR-Bench - writes a PNG of a window, with alpha, for the README and the docs.
#
#   scripts\screenshot.ps1 -Window rack  -Out docs\screenshots\rack-dark.png [-Delay 6]
#   scripts\screenshot.ps1 -Window scope -Out docs\screenshots\scope-dark.png -Wire scope:1=4,scope:2=5
#
# Runs the bench in its screenshot mode: it comes up, connects to the probe,
# opens the instrument and the wires asked for, and after the delay saves its
# own framebuffer (so the transparent margins stay transparent, whatever is
# behind the window) and quits. The emulator should be running with the
# probe for the ports to show activity.
param(
    [string]$Window = 'rack',
    [string]$Out = "docs\screenshots\rack-dark.png",
    [string]$Wire = '',
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

$argv = @('--screenshot', "`"$Window=$Out`"", '--delay', "$Delay")
if ($Window -ne 'rack') { $argv += @('--open', $Window) }
foreach ($w in ($Wire -split ',' | Where-Object { $_ })) { $argv += @('--wire', $w) }

# A GUI executable returns at once: wait for it explicitly.
$p = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -Wait
if ($p.ExitCode -ne 0 -or -not (Test-Path $Out)) {
    Write-Error "Screenshot failed (exit $($p.ExitCode))."
}

# The bench writes uncompressed PNGs (no zlib dependency): recompress them so
# the repository stays small. The alpha channel is preserved.
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($Out)
$bmp = New-Object System.Drawing.Bitmap $src
$src.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output $Out
