# RTR-Bench - regenerates the screenshots the README shows, on the demo probe,
# in the dark theme. Open every image it writes and check it before a commit. Run after a change of any front panel, before a
# release (directive in CLAUDE.md). The emulator scenes (rack-dark.png,
# scope-dark.png) are taken by hand with scripts\screenshot.ps1 while the
# RTR-OS counter runs.
#
#   scripts\shots.ps1            every scene
#   scripts\shots.ps1 -Only gen  one scene (bench, demo, gen, psu, dmm, logic, scope, rack, lab, benchlab)
#
# The rack shows the audio devices of the computer the scenes are taken on.
# The composite scenes let the bench place the windows (--tile): their sizes
# depend on the instruments and on how many audio devices the rack lists.
param(
    [string]$Only = '',
    [double]$Delay = 5
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\rtr-bench.exe'
$d = Join-Path $root 'docs\screenshots'
if (-not (Test-Path $exe)) { Write-Error "Not built yet: run scripts\build.ps1 first." }
if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
Add-Type -AssemblyName System.Drawing

# Demo probe ports: D0-D8 = 0-8, A1-A4 = 9-12, IN0-IN3 = 13-16, AO0-AO1 = 17-18.
# Every scene lets the bench lay its windows out (--tile): none over another.
$demo = @('--probe', 'demo', '--delay', "$Delay", '--tile', 'on')
$scenes = @{
    # The whole bench: every instrument, wired, including instrument-to-instrument links.
    bench = @('--open', 'gen', '--open', 'psu', '--open', 'dmm', '--open', 'scope', '--open', 'logic',
              '--gen', '1=on', '--psu', '1=3.3',
              '--wire', 'gen:1=13', '--wire', 'psu:1=14',
              '--wire', 'scope:1=13', '--wire', 'scope:2=9', '--wire', 'scope:3=14',
              '--wire', 'logic:1=0', '--wire', 'logic:2=1', '--wire', 'logic:3=2', '--wire', 'logic:4=3',
              '--wire', 'dmm:1=13')
    # The rack and the oscilloscope: a sine and a triangle, two digital channels, M1 = CH1 x CH2.
    demo  = @('--open', 'scope',
              '--wire', 'scope:1=9', '--wire', 'scope:2=10', '--wire', 'scope:3=0', '--wire', 'scope:4=1',
              '--math', '"1=A x B"')
    gen   = @('--open', 'gen', '--gen', '1=on', '--wire', 'gen:1=13')
    psu   = @('--open', 'psu', '--psu', '1=3.3', '--wire', 'psu:1=14')
    dmm   = @('--open', 'dmm', '--wire', 'dmm:1=0')
    logic = @('--open', 'logic', '--wire', 'logic:1=0', '--wire', 'logic:2=1', '--wire', 'logic:3=2', '--wire', 'logic:4=3')
    scope = @('--open', 'scope', '--wire', 'scope:1=9', '--wire', 'scope:2=10', '--wire', 'scope:3=0', '--wire', 'scope:4=1',
              '--math', '"1=A x B"')
    rack  = @()
    # The circuit bench alone: a transistor dimming an LED, real parts of the
    # catalog, panel meters, the properties panel of the transistor.
    lab   = @('--open', 'psu', '--open', 'lab', '--circuit', "`"$root\docs\circuits\led-dimmer.json`"", '--psu', '1=9')
    # The circuit bench with the instruments: generator into an RC low-pass
    # on the oscilloscope, the supply lighting an LED. No more instruments
    # than fit one monitor in two columns: a window that falls off the screen
    # moves to another monitor and takes its scale.
    benchlab = @('--open', 'lab', '--open', 'gen', '--open', 'psu', '--open', 'scope',
                 '--circuit', "`"$root\docs\circuits\rc-led.json`"",
                 '--wire', 'gen:1=20000', '--wire', 'scope:1=20001', '--wire', 'scope:2=20002',
                 '--wire', 'psu:1=20003', '--gen', '1=on', '--psu', '1=5')
}
# Scene -> (screenshot target, file stem).
$targets = @{
    bench = @('bench', 'bench-all'); demo = @('bench', 'bench-demo')
    gen = @('gen', 'gen'); psu = @('psu', 'psu'); dmm = @('dmm', 'dmm'); logic = @('logic', 'logic')
    scope = @('scope', 'scope-demo'); rack = @('rack', 'rack-demo')
    lab = @('lab', 'lab'); benchlab = @('bench', 'bench-lab')
}

function Recompress($file) {
    # The bench writes uncompressed PNGs (no zlib dependency); keep the repository small.
    $src = [System.Drawing.Image]::FromFile($file)
    $bmp = New-Object System.Drawing.Bitmap $src
    $src.Dispose()
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

foreach ($name in @('bench', 'demo', 'gen', 'psu', 'dmm', 'logic', 'scope', 'rack', 'lab', 'benchlab')) {
    if ($Only -and $name -ne $Only) { continue }
    $target, $stem = $targets[$name]
    foreach ($theme in @('dark')) {
        $out = Join-Path $d "$stem-$theme.png"
        $args = $demo + $scenes[$name] + @('--theme', $theme, '--screenshot', "`"$target=$out`"")
        $p = Start-Process -FilePath $exe -ArgumentList $args -PassThru -Wait
        if ($p.ExitCode -ne 0 -or -not (Test-Path $out)) { Write-Error "Scene $name ($theme) failed (exit $($p.ExitCode))." }
        Recompress $out
        Write-Output $out
    }
}
