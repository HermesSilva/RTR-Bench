# RTR-Bench - fetches the ngspice shared library (the circuit simulator the
# circuit bench loads at run time) and puts ngspice.dll next to rtr-bench.exe.
#
#   -Version   ngspice release to fetch (default 46: the package of 47 needs
#              sndfile.dll and samplerate.dll, which it does not carry)
#   -Into      folder that receives ngspice.dll (default build\)
param(
    [string]$Version = '46',
    [string]$Into
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Into) { $Into = Join-Path $root 'build' }
$work = Join-Path $env:TEMP "rtr-bench-ngspice-$Version"
$archive = Join-Path $work "ngspice-${Version}_dll_64.7z"
# The current release lives in its own folder, the earlier ones in old-releases.
$base = 'https://sourceforge.net/projects/ngspice/files/ng-spice-rework'
$urls = @("$base/$Version/ngspice-${Version}_dll_64.7z/download",
          "$base/old-releases/$Version/ngspice-${Version}_dll_64.7z/download")

New-Item -ItemType Directory -Force $work | Out-Null
if (-not (Test-Path $archive)) {
    foreach ($url in $urls) {
        Write-Output "downloading $url"
        curl.exe -L --fail --silent --show-error -o $archive $url
        if ($LASTEXITCODE -eq 0) { break }
        if (Test-Path $archive) { Remove-Item $archive -Force }
    }
    if (-not (Test-Path $archive)) { Write-Error "download failed" }
}

# 7-Zip unpacks it; the tar.exe of Windows 11 reads 7z too (that of Windows 10
# has no LZMA).
$unpacked = Join-Path $work 'unpacked'
if (Test-Path $unpacked) { [System.IO.Directory]::Delete($unpacked, $true) }
New-Item -ItemType Directory -Force $unpacked | Out-Null
$sevenZip = (Get-Command 7z.exe -ErrorAction SilentlyContinue).Source
if (-not $sevenZip -and (Test-Path "$env:ProgramFiles\7-Zip\7z.exe")) { $sevenZip = "$env:ProgramFiles\7-Zip\7z.exe" }
if ($sevenZip) {
    & $sevenZip x $archive "-o$unpacked" -y | Out-Null
} else {
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $unpacked
}
if ($LASTEXITCODE -ne 0) { Write-Error "cannot unpack $archive (install 7-Zip)" }

$dll = Get-ChildItem -Path $unpacked -Recurse -Filter 'ngspice.dll' | Select-Object -First 1
if (-not $dll) { Write-Error "ngspice.dll not found in the archive" }
New-Item -ItemType Directory -Force $Into | Out-Null
Copy-Item $dll.FullName (Join-Path $Into 'ngspice.dll') -Force
# The OpenMP runtime the library is built with travels in the same folder.
Get-ChildItem -Path $dll.DirectoryName -Filter '*.dll' | Where-Object { $_.Name -ne 'ngspice.dll' } | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $Into $_.Name) -Force
}
Write-Output "ngspice $Version -> $(Join-Path $Into 'ngspice.dll')"
