# Removes the HW2 Frame VR proxy from HomeworldRM\Bin\Release and restores any opengl32.dll it backed up.
# hw2vr.ini and hw2vr.log are kept unless -Purge.
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
if ($Purge) {
    foreach ($f in 'hw2vr.ini', 'hw2vr.log') { Remove-Item (Join-Path $GameDir $f) -ErrorAction SilentlyContinue }
}
Remove-Item (Join-Path $GameDir $Hw2vrManifest) -ErrorAction SilentlyContinue

$left = @('hw2vr.ini', 'hw2vr.log' | Where-Object { Test-Path (Join-Path $GameDir $_) })
Write-Host ("HW2 Frame VR removed from $GameDir" + $(if ($left) { " (kept: $($left -join ', '); -Purge removes them)" } else { '' }))
