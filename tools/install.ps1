# Installs the HW2 Frame VR proxy (opengl32.dll) into HomeworldRM\Bin\Release.
# Never modifies game files: an existing non-HW2VR opengl32.dll (e.g. ReShade) is renamed to a backup that
# uninstall.ps1 restores. hw2vr.ini is copied only if the game folder doesn't have one yet.
# -ResetConfig replaces an existing hw2vr.ini with the shipped one (old copy kept as hw2vr.ini.bak).
# -LargeAddressAware (opt-in, the one change to a game file): sets the Large Address Aware bit in HomeworldRM.exe so
# the 32-bit game gets 4 GB of address space - needed for a 2880x2880 VR window. The original exe is kept as
# HomeworldRM.exe.hw2vr-original and uninstall.ps1 puts it back. A Steam update or file check undoes the patch; the
# proxy logs whether it is on.
# Usage: pwsh -File tools\install.ps1 [-GameDir <...\HomeworldRM\Bin\Release>] [-Config RelWithDebInfo|Debug] [-ResetConfig]
#                                     [-LargeAddressAware]
param([string]$GameDir, [string]$Config = 'RelWithDebInfo', [switch]$ResetConfig, [switch]$LargeAddressAware)
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
$manifest = Join-Path $GameDir $Hw2vrManifest
$previous = if (Test-Path $manifest) { @(Get-Content $manifest) } else { @() }
$placed = @('opengl32.dll')

if ((Test-Path $target) -and -not (Test-Hw2vrDll $target)) {
    if (Test-Path $backup) { throw "Both a foreign opengl32.dll and $Hw2vrBackupName exist in $GameDir - sort that out by hand." }
    Rename-Item $target $Hw2vrBackupName
    Write-Warning "Existing opengl32.dll (not ours, e.g. ReShade) backed up to $Hw2vrBackupName; uninstall restores it."
}
Copy-Item $dll $target -Force
$pdb = Join-Path $build 'opengl32.pdb'
if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $GameDir 'opengl32.pdb') -Force; $placed += 'opengl32.pdb' }
# 32-bit OpenVR client, loaded by the proxy at runtime. The game ships none; back up a foreign one just in case.
$openvr = Join-Path $GameDir 'openvr_api.dll'
if ((Test-Path $openvr) -and ('openvr_api.dll' -notin $previous)) {
    Rename-Item $openvr $Hw2vrOpenvrBackupName
    Write-Warning "Existing openvr_api.dll (not ours) backed up to $Hw2vrOpenvrBackupName; uninstall restores it."
}
Copy-Item (Join-Path $build 'openvr_api.dll') $openvr -Force
$placed += 'openvr_api.dll'
# SteamVR Input: action manifest + Steam Frame default binding (bindings/generate_bindings.py), always replaced.
$bindings = Join-Path $GameDir 'hw2vr_bindings'
New-Item -ItemType Directory -Force $bindings | Out-Null
Copy-Item (Join-Path $root 'bindings\*.json') $bindings -Force
$placed += 'hw2vr_bindings'
$ini = Join-Path $GameDir 'hw2vr.ini'
if ($ResetConfig -and (Test-Path $ini)) { Move-Item $ini "$ini.bak" -Force; Write-Host "Old hw2vr.ini kept as hw2vr.ini.bak" }
if (-not (Test-Path $ini)) {
    Copy-Item (Join-Path $root 'config\hw2vr.ini') $GameDir
    $placed += 'hw2vr.ini'
}
if ($LargeAddressAware) {
    $exe = Join-Path $GameDir 'HomeworldRM.exe'
    if (Test-LargeAddressAware $exe) {
        Write-Host 'HomeworldRM.exe is already Large Address Aware.'
    } else {
        $exeBackup = Join-Path $GameDir $Hw2vrExeBackupName
        Copy-Item $exe $exeBackup -Force
        $bytes = [IO.File]::ReadAllBytes($exe)
        $at = Get-LaaBitOffset $bytes
        $bytes[$at] = $bytes[$at] -bor 0x20
        [IO.File]::WriteAllBytes($exe, $bytes)
        Write-Host "HomeworldRM.exe patched to be Large Address Aware (original kept as $Hw2vrExeBackupName)"
    }
    $placed += 'HomeworldRM.exe (Large Address Aware)'
}
# Keep entries from an earlier install (e.g. hw2vr.ini placed then) so uninstall still knows about them.
Set-Content $manifest (@($previous) + $placed | Select-Object -Unique)
$placed = @(Get-Content $manifest)

$v = (Get-Item $target).VersionInfo
Write-Host "Installed $($v.ProductName) $($v.FileVersion) ($Config) to $GameDir"
Write-Host "  files: $($placed -join ', ')"
Write-Host '  log:   hw2vr.log next to the DLL (rewritten each launch)'
Write-Host '  NOTE:  single-player/skirmish mod (no multiplayer guard, by decision).' -ForegroundColor Yellow
