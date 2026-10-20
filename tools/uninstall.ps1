# Removes the HW2 Frame VR proxy from HomeworldRM\Bin\Release and restores any opengl32.dll it backed up.
# hw2vr.ini, hw2vr_console.ini / hw2vr_panel.ini (saved placements) and hw2vr.log are kept unless -Purge.
# Usage: pwsh -File tools\uninstall.ps1 [-GameDir <...\HomeworldRM\Bin\Release>] [-Purge]
param([string]$GameDir, [switch]$Purge)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GameDir.ps1')

if (-not $GameDir) { $GameDir = Find-HwrmBinDir }
Assert-GameNotRunning

$target = Join-Path $GameDir 'opengl32.dll'
if (Test-Hw2vrDll $target) {
    Remove-Item $target
    Remove-Item (Join-Path $GameDir 'opengl32.pdb') -ErrorAction SilentlyContinue
} elseif (Test-Path $target) {
    Write-Warning "$target is not ours (ProductName '$((Get-Item $target).VersionInfo.ProductName)') - leaving it alone."
}

$backup = Join-Path $GameDir $Hw2vrBackupName
if (Test-Path $backup) {
    if (Test-Path $target) {
        Write-Warning "Not restoring $Hw2vrBackupName - an opengl32.dll that isn't ours is in the way."
    } else {
        Rename-Item $backup 'opengl32.dll'
        Write-Host "Restored the original opengl32.dll from $Hw2vrBackupName"
    }
}
$manifest = Join-Path $GameDir $Hw2vrManifest
$placed = if (Test-Path $manifest) { @(Get-Content $manifest) } else { @() }
$openvr = Join-Path $GameDir 'openvr_api.dll'
if ('openvr_api.dll' -in $placed) { Remove-Item $openvr -ErrorAction SilentlyContinue }
$openvrBackup = Join-Path $GameDir $Hw2vrOpenvrBackupName
if ((Test-Path $openvrBackup) -and -not (Test-Path $openvr)) {
    Rename-Item $openvrBackup 'openvr_api.dll'
    Write-Host "Restored the original openvr_api.dll from $Hw2vrOpenvrBackupName"
}
Remove-Item (Join-Path $GameDir 'hw2vr_bindings') -Recurse -Force -ErrorAction SilentlyContinue
# The Large Address Aware patch: put the original exe back - unless Steam has replaced the exe since (then the backup
# is stale and the exe already unpatched or newer: only the backup goes).
$exe = Join-Path $GameDir 'HomeworldRM.exe'
$exeBackup = Join-Path $GameDir $Hw2vrExeBackupName
if (Test-Path $exeBackup) {
    $now = [IO.File]::ReadAllBytes($exe)
    $orig = [IO.File]::ReadAllBytes($exeBackup)
    $at = Get-LaaBitOffset $orig
    $orig[$at] = $orig[$at] -bor 0x20
    if ($now.Length -eq $orig.Length -and [Linq.Enumerable]::SequenceEqual($now, $orig)) {
        Copy-Item $exeBackup $exe -Force
        Write-Host "Restored the original HomeworldRM.exe (Large Address Aware patch removed)"
    } else {
        Write-Warning "HomeworldRM.exe changed since it was patched (Steam update?) - left as it is; stale backup removed."
    }
    Remove-Item $exeBackup
}
if ($Purge) {
    foreach ($f in 'hw2vr.ini', 'hw2vr.ini.bak', 'hw2vr.log', 'hw2vr_panel.ini', 'hw2vr_console.ini') { Remove-Item (Join-Path $GameDir $f) -ErrorAction SilentlyContinue }
}
Remove-Item $manifest -ErrorAction SilentlyContinue

$left = @('hw2vr.ini', 'hw2vr.log', 'hw2vr_panel.ini', 'hw2vr_console.ini' | Where-Object { Test-Path (Join-Path $GameDir $_) })
Write-Host ("HW2 Frame VR removed from $GameDir" + $(if ($left) { " (kept: $($left -join ', '); -Purge removes them)" } else { '' }))
