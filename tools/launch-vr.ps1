# Starts Homeworld 2 Remastered straight into VR: Steam and SteamVR first (if they aren't running), then the game in
# a square window (each eye gets even pixels per degree) with -hw2vr, so VR starts with it and the menus appear in the
# headset. The window is taller than the monitor - fine, the laser sends its mouse events straight to the window.
# Needs the Large Address Aware patch for windows bigger than 3440x1440 (pwsh -File tools\install.ps1 -LargeAddressAware).
# Usage: pwsh -File tools\launch-vr.ps1 [-Size 2880] [-GameDir <...\HomeworldRM\Bin\Release>] [-Extra '<more game args>']
param([int]$Size = 2880, [string]$GameDir, [string]$Extra = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GameDir.ps1')
if (-not $GameDir) { $GameDir = Find-HwrmBinDir }
Assert-GameNotRunning
$exe = Join-Path $GameDir 'HomeworldRM.exe'
if (-not (Test-Hw2vrDll (Join-Path $GameDir 'opengl32.dll'))) { throw 'HW2 Frame VR is not installed - run tools\install.ps1.' }
if ($Size * $Size -gt 3440 * 1440 -and -not (Test-LargeAddressAware $exe)) {
    throw "A ${Size}x${Size} window needs more than the game's 2 GB of memory - run: pwsh -File tools\install.ps1 -LargeAddressAware"
}

function Wait-Process([string]$name, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while (-not (Get-Process $name -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep 1 }
    [bool](Get-Process $name -ErrorAction SilentlyContinue)
}

if (-not (Get-Process steam -ErrorAction SilentlyContinue)) {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamExe
    Start-Process $steam -ArgumentList '-silent'
    if (-not (Wait-Process steam 60)) { throw 'Steam did not start.' }
    Start-Sleep 10  # let it log in before the game asks for it
}
if (-not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
    Start-Process 'steam://rungameid/250820'  # SteamVR
    if (-not (Wait-Process vrserver 90)) { throw 'SteamVR did not start.' }
    Start-Sleep 5
}
# Brendo's HW2 launch line (docs: memory hwrm-target-facts), at the square size, plus -hw2vr. -noMovies: the intro
# videos (Gearbox logo first) look like a frozen screen on the panel and invite a press of the system button - which
# opens SteamVR's dashboard with its desktop view in front of the game.
$gameArgs = "-forceResolution -w $Size -h $Size -dlccampaign HW2Campaign.big -campaign Ascension -moviepath DataHW2Campaign -noMovies -hw2vr $Extra".Trim()
Start-Process -FilePath $exe -WorkingDirectory $GameDir -ArgumentList $gameArgs
Write-Host "Started HomeworldRM.exe $gameArgs"
Write-Host 'Put the headset on: the main menu appears in front of you; point with the right hand, bumper = click.'
Write-Host "If you see SteamVR's dashboard (your desktop on a big screen) instead, press the controller's system button to close it."
