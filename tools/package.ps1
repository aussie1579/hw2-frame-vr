# Builds the release zip an end user installs with nothing but the game, SteamVR and Windows: our opengl32.dll,
# Valve's openvr_api.dll, hw2vr.ini, the SteamVR bindings, the install/uninstall/launch scripts and double-click
# wrappers (tools\package\*.cmd). No game files: the proxy reads the game's icons from the player's own install.
# Builds and smoke-tests first (tools\smoke-test.ps1) unless -NoBuild.
# Usage: pwsh -File tools\package.ps1 [-NoBuild]   ->  dist\HW2FrameVR-<version>.zip
param([switch]$NoBuild)
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot
$config = 'RelWithDebInfo'  # the configuration tested in the headset
if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Config $config
    if ($LASTEXITCODE) { throw 'Smoke test failed - not packaging.' }
}
. (Join-Path $PSScriptRoot 'GameDir.ps1')
$build = Join-Path $root "build\win32\$config"
$dll = Join-Path $build 'opengl32.dll'
if (-not (Test-Hw2vrDll $dll)) { throw "No HW2 Frame VR build at $dll." }
$version = (Get-Item $dll).VersionInfo.ProductVersion

$name = "HW2FrameVR-$version"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
$zip = Join-Path $dist "$name.zip"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
foreach ($d in 'bin', 'config', 'bindings', 'tools', 'licenses') { New-Item -ItemType Directory -Force (Join-Path $stage $d) | Out-Null }

Copy-Item $dll, (Join-Path $build 'openvr_api.dll') (Join-Path $stage 'bin')
Copy-Item (Join-Path $root 'config\hw2vr.ini') (Join-Path $stage 'config')
Copy-Item (Join-Path $root 'bindings\*.json') (Join-Path $stage 'bindings')
foreach ($s in 'install.ps1', 'uninstall.ps1', 'launch-vr.ps1', 'GameDir.ps1') { Copy-Item (Join-Path $PSScriptRoot $s) (Join-Path $stage 'tools') }
Copy-Item (Join-Path $PSScriptRoot 'package\*.cmd') $stage
Copy-Item (Join-Path $root 'README.md') $stage
Copy-Item (Join-Path $root 'third_party\openvr\LICENSE') (Join-Path $stage 'licenses\OpenVR-LICENSE.txt')
Set-Content (Join-Path $stage 'licenses\stb_image.txt') @'
stb_image (https://github.com/nothings/stb) is compiled into opengl32.dll to decode the game's icons.
It is in the public domain, or available under the MIT license - see the end of stb_image.h.
'@

Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Packaged $name -> $zip ($([math]::Round((Get-Item $zip).Length / 1KB)) KB)"
Get-ChildItem $stage -Recurse -File | ForEach-Object { '  ' + $_.FullName.Substring($stage.Length + 1) }
