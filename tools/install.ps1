# Installs the HW2 Frame VR proxy (opengl32.dll) into HomeworldRM\Bin\Release.
# Never modifies game files: an existing non-HW2VR opengl32.dll (e.g. ReShade) is renamed to a backup that
# uninstall.ps1 restores. hw2vr.ini is copied only if the game folder doesn't have one yet.
# Usage: pwsh -File tools\install.ps1 [-GameDir <...\HomeworldRM\Bin\Release>] [-Config RelWithDebInfo|Debug]
param([string]$GameDir, [string]$Config = 'RelWithDebInfo')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GameDir.ps1')

$root = Split-Path $PSScriptRoot
$build = Join-Path $root "build\win32\$Config"
$dll = Join-Path $build 'opengl32.dll'
if (-not (Test-Hw2vrDll $dll)) { throw "No HW2 Frame VR build at $dll - run tools\smoke-test.ps1 (builds + tests) first." }
if (-not $GameDir) { $GameDir = Find-HwrmBinDir }
Assert-GameNotRunning

$target = Join-Path $GameDir 'opengl32.dll'
$backup = Join-Path $GameDir $Hw2vrBackupName
$placed = @('opengl32.dll')

if ((Test-Path $target) -and -not (Test-Hw2vrDll $target)) {
    if (Test-Path $backup) { throw "Both a foreign opengl32.dll and $Hw2vrBackupName exist in $GameDir - sort that out by hand." }
    Rename-Item $target $Hw2vrBackupName
    Write-Warning "Existing opengl32.dll (not ours, e.g. ReShade) backed up to $Hw2vrBackupName; uninstall restores it."
}
Copy-Item $dll $target -Force
$pdb = Join-Path $build 'opengl32.pdb'
if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $GameDir 'opengl32.pdb') -Force; $placed += 'opengl32.pdb' }
if (-not (Test-Path (Join-Path $GameDir 'hw2vr.ini'))) {
    Copy-Item (Join-Path $root 'config\hw2vr.ini') $GameDir
    $placed += 'hw2vr.ini'
}
# Keep entries from an earlier install (e.g. hw2vr.ini placed then) so uninstall still knows about them.
$manifest = Join-Path $GameDir $Hw2vrManifest
if (Test-Path $manifest) { $placed = @(Get-Content $manifest) + $placed | Select-Object -Unique }
Set-Content $manifest $placed

$v = (Get-Item $target).VersionInfo
Write-Host "Installed $($v.ProductName) $($v.FileVersion) ($Config) to $GameDir"
Write-Host "  files: $($placed -join ', ')"
Write-Host '  log:   hw2vr.log next to the DLL (rewritten each launch)'
Write-Host '  NOTE:  single-player/skirmish only - there is no multiplayer guard yet. Uninstall before playing online.' -ForegroundColor Yellow
