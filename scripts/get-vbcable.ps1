# RTR-Bench - fetches the installer of VB-CABLE (VB-Audio), a virtual audio
# cable for Windows signed by Microsoft, into extras\vb-cable\. With it a
# player sends its sound to "CABLE Input" and the bench finds the other end,
# "CABLE Output", as an IN group on the rack.
#
# VB-CABLE is donationware of VB-Audio Software, not part of this project and
# not open source: the installer is fetched from its site and is not kept in
# the repository. See https://vb-audio.com/Cable/ for its terms.
#
#   -Pack   driver pack to fetch (default 45)
param(
    [string]$Pack = '45'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$folder = Join-Path $root 'extras\vb-cable'
$archive = Join-Path $folder "VBCABLE_Driver_Pack$Pack.zip"
$url = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack$Pack.zip"

New-Item -ItemType Directory -Force $folder | Out-Null
Write-Output "downloading $url"
curl.exe -L --fail --silent --show-error -o $archive $url
if ($LASTEXITCODE -ne 0) { Write-Error 'download failed: see https://vb-audio.com/Cable/ for the current pack' }

$unpacked = Join-Path $folder "Pack$Pack"
if (Test-Path $unpacked) { [System.IO.Directory]::Delete($unpacked, $true) }
Expand-Archive -Path $archive -DestinationPath $unpacked
$setup = Join-Path $unpacked 'VBCABLE_Setup_x64.exe'
if (-not (Test-Path $setup)) { Write-Error "VBCABLE_Setup_x64.exe not found in $unpacked" }
Write-Output "installer: $setup"
Write-Output 'Run it as administrator (right click, Run as administrator), then restart Windows.'
