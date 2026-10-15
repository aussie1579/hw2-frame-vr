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
    Push-Location $root  # presets are read from the current directory
    try {
        & $cmake --preset win32 | Out-Null
        $preset = if ($Config -eq 'Debug') { 'debug' } else { 'release' }
        & $cmake --build --preset $preset
        if ($LASTEXITCODE) { throw "build failed" }
    } finally { Pop-Location }
}

$failed = 0
# The UI phase hook's signature is checked against the installed game (mapped as an image, never run).
. (Join-Path $PSScriptRoot 'GameDir.ps1')
try { $env:HW2VR_GAME_EXE = Join-Path (Find-HwrmBinDir) 'HomeworldRM.exe' } catch { Remove-Item Env:HW2VR_GAME_EXE -ErrorAction SilentlyContinue }
Write-Host "`n=== math test (per-eye camera maths) ===" -ForegroundColor Cyan
& (Join-Path $out 'hw2vr_math_test.exe')
if ($LASTEXITCODE) { $failed++ }
foreach ($mode in 'off', 'yaw+offset') {
    Set-Content $ini "[spike]`r`nstart_mode = $mode`r`nyaw_degrees = 10`r`noffset_x = 150`r`n" -Encoding ascii
    Write-Host "`n=== smoke test, spike start_mode = $mode ===" -ForegroundColor Cyan
    & (Join-Path $out 'hw2vr_smoke.exe')
    if ($LASTEXITCODE) { $failed++ }
}
# HUD pipeline without VR: capture + alpha conversion + clear + cursor redirect, read back from the textures.
Set-Content $ini "[debug]`r`nhud_capture = 1`r`n" -Encoding ascii
Write-Host "`n=== smoke test, HUD pipeline (forced capture, no VR) ===" -ForegroundColor Cyan
& (Join-Path $out 'hw2vr_smoke.exe')
if ($LASTEXITCODE) { $failed++ }

# Second-eye replay without VR: every frame recorded and replayed at the swap with a shifted camera.
Set-Content $ini "[debug]`r`nreplay_test_offset_x = 0.25`r`n" -Encoding ascii
Write-Host "`n=== smoke test, frame replay (synchronized stereo machinery, no VR) ===" -ForegroundColor Cyan
& (Join-Path $out 'hw2vr_smoke.exe')
if ($LASTEXITCODE) { $failed++ }

# VR autostart: with a headset (SteamVR running) VR must start; without one the proxy must fall back to flat
# cleanly. Either way every GL check must still pass.
Set-Content $ini "[vr]`r`nautostart = 1`r`n" -Encoding ascii
Write-Host "`n=== smoke test, VR autostart (SteamVR running: $([bool](Get-Process vrserver -ErrorAction SilentlyContinue))) ===" -ForegroundColor Cyan
& (Join-Path $out 'hw2vr_smoke.exe') | Select-Object -Last 1
if ($LASTEXITCODE) { $failed++ }
$vrLines = @(Select-String -Path (Join-Path $out 'hw2vr.log') -Pattern ' vr: ' | ForEach-Object { $_.Line })
$vrLines | ForEach-Object { "  log: $_" }
if (-not ($vrLines -match 'vr: started|staying flat')) { Write-Host '[FAIL] VR autostart neither started nor fell back to flat' -ForegroundColor Red; $failed++ }
else { Write-Host '[ OK ] VR autostart handled (started, or fell back to flat)' }

Remove-Item $ini
Write-Host ("`n" + $(if ($failed) { "SMOKE TEST FAILED ($failed run(s))" } else { 'SMOKE TEST PASSED (all runs)' })) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
exit $failed
