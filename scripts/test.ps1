# RTR-Bench - runs the windowless tests built by build.ps1.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
ctest --test-dir (Join-Path $root 'build') --output-on-failure
exit $LASTEXITCODE
