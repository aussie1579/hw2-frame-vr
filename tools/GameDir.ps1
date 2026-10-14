# Dot-sourced by install.ps1 / uninstall.ps1. Finds HomeworldRM\Bin\Release through Steam's library list.

$script:Hw2vrProductName = 'HW2 Frame VR'   # ProductName in our DLL's version resource (src/common/version.h)
$script:Hw2vrManifest    = 'hw2vr-installed.txt'
$script:Hw2vrBackupName  = 'opengl32.dll.hw2vr-backup'
$script:Hw2vrOpenvrBackupName = 'openvr_api.dll.hw2vr-backup'

function Find-HwrmBinDir {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { throw 'Steam not found in the registry; pass -GameDir.' }
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    $libraries = @($steam) + @(Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' })
    foreach ($lib in $libraries | Select-Object -Unique) {
        $manifest = Join-Path $lib 'steamapps\appmanifest_244160.acf'
        if (-not (Test-Path $manifest)) { continue }
        $installdir = (Select-String -Path $manifest -Pattern '"installdir"\s+"([^"]+)"').Matches[0].Groups[1].Value
        $bin = Join-Path $lib "steamapps\common\$installdir\HomeworldRM\Bin\Release"
        if (Test-Path (Join-Path $bin 'HomeworldRM.exe')) { return $bin }
    }
    throw 'Homeworld Remastered (app 244160) not found in any Steam library; pass -GameDir.'
}

function Test-Hw2vrDll([string]$path) {
    (Test-Path $path) -and ((Get-Item $path).VersionInfo.ProductName -eq $script:Hw2vrProductName)
}

function Assert-GameNotRunning {
    if (Get-Process HomeworldRM -ErrorAction SilentlyContinue) { throw 'HomeworldRM.exe is running - quit the game first.' }
}
