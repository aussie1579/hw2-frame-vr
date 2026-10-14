# Builds the proxy and runs tests/proxy_smoke.cpp twice: override off (passthrough) and yaw+offset (override math
# and uniform classification). No game needed.
# Usage: pwsh -File tools\smoke-test.ps1 [-Config RelWithDebInfo|Debug] [-NoBuild]
param([string]$Config = 'RelWithDebInfo', [switch]$NoBuild)
$ErrorActionPreference = 'Stop'

$root  = Split-Path $PSScriptRoot
$cmake = 'E:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$out   = Join-Path $root "build\win32\$Config"
$ini   = Join-Path $out 'hw2vr.ini'

if (-not $NoBuild) {
    & $cmake --preset win32 | Out-Null
    $preset = if ($Config -eq 'Debug') { 'debug' } else { 'release' }
    & $cmake --build --preset $preset
    if ($LASTEXITCODE) { throw "build failed" }
}

$failed = 0
foreach ($mode in 'off', 'yaw+offset') {
    Set-Content $ini "[spike]`r`nstart_mode = $mode`r`nyaw_degrees = 10`r`noffset_x = 150`r`n" -Encoding ascii
    Write-Host "`n=== smoke test, spike start_mode = $mode ===" -ForegroundColor Cyan
    & (Join-Path $out 'hw2vr_smoke.exe')
    if ($LASTEXITCODE) { $failed++ }
}
Remove-Item $ini
Write-Host ("`n" + $(if ($failed) { "SMOKE TEST FAILED ($failed run(s))" } else { 'SMOKE TEST PASSED (both runs)' })) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
exit $failed
