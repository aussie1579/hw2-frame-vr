# Starts the RE environment for HWRM: Ghidra 11.3.2 on the HWRM project with HomeworldRM.exe open
# in CodeBrowser (GhidraMCP serves on 127.0.0.1:8080), and Cheat Engine with the MCP bridge autorun.
# Usage: pwsh -File tools\start-re.ps1 [-NoGhidra] [-NoCheatEngine]
param([switch]$NoGhidra, [switch]$NoCheatEngine)

$ghidra  = "$env:USERPROFILE\Downloads\ghidra_11.3.2_PUBLIC_20250415\ghidra_11.3.2_PUBLIC"
$ghPrefs = "$env:APPDATA\ghidra\ghidra_11.3.2_PUBLIC\preferences"
$ghLog   = "$env:APPDATA\ghidra\ghidra_11.3.2_PUBLIC\application.log"
$project = Join-Path (Split-Path $PSScriptRoot) 're\ghidra\HWRM'
$ceExe   = 'C:\Program Files\Cheat Engine\cheatengine-x86_64.exe'

function Test-GhidraRunning {
    @(Get-CimInstance Win32_Process -Filter "Name='javaw.exe' OR Name='java.exe'" |
      Where-Object CommandLine -match 'ghidra_11\.3\.2').Count -gt 0
}
function Test-GhidraMcpUp { [bool](Get-NetTCPConnection -LocalPort 8080 -State Listen -ErrorAction SilentlyContinue) }

if (-not $NoGhidra) {
    if (-not (Test-Path "$project.gpr")) { throw "Ghidra project $project.gpr not found - run the headless import first (see docs/FINDINGS.md)." }
    if (Test-GhidraMcpUp) {
        Write-Host 'Ghidra: GhidraMCP already listening on 8080 - nothing to do.'
    } else {
        if (-not (Test-GhidraRunning)) {
            # Ghidra (GUI and headless) rewrites preferences on exit, so set the last project right before launch.
            $escaped = $project -replace '\\', '\\' -replace ':', '\:'
            (Get-Content $ghPrefs) -replace '^LastOpenedProject=.*', "LastOpenedProject=$escaped" | Set-Content $ghPrefs -Encoding ascii
            $launched = (Get-Date).AddSeconds(-1)
            Start-Process "$ghidra\ghidraRun.bat" -WorkingDirectory $ghidra -WindowStyle Hidden
            # Only trust listener lines logged after this launch. If GhidraGo is called before the listener is up,
            # it starts a second Ghidra that finds the project locked and opens the program read-only.
            $deadline = (Get-Date).AddSeconds(90)
            do {
                Start-Sleep 2
                $ready = Select-String -Path $ghLog -Pattern 'Listening for GhidraGo Requests' | Where-Object {
                    [datetime]::ParseExact($_.Line.Substring(0, 19), 'yyyy-MM-dd HH:mm:ss', $null) -ge $launched }
            } until ($ready -or (Get-Date) -gt $deadline)
            if (-not $ready) { throw 'Ghidra did not start its GhidraGo listener within 90 s.' }
            # If Ghidra was last closed with CodeBrowser open, the project restores it (and GhidraMCP) by itself.
            $deadline = (Get-Date).AddSeconds(8)
            while (-not (Test-GhidraMcpUp) -and (Get-Date) -lt $deadline) { Start-Sleep 1 }
        }
        if (Test-GhidraMcpUp) {
            Write-Host 'Ghidra: CodeBrowser restored from project state; GhidraMCP on 8080: up'
        } else {
            # Fallback when the project has no saved CodeBrowser. GhidraGo opens programs by URL, which Ghidra treats
            # as READ-ONLY (MCP renames won't save). Fix: in the project window double-click HomeworldRM.exe, then
            # close the read-only CodeBrowser; Ghidra then saves a writable session for next time.
            # ghidraGo.bat wraps its args in "%*", so pass the URL unquoted through cmd (the Ghidra path has no spaces).
            $url = "ghidra:/$($project -replace '\\', '/')?/HomeworldRM.exe"
            cmd /c "$ghidra\support\GhidraGo\ghidraGo.bat $url" | Out-Null
            $deadline = (Get-Date).AddSeconds(60)
            while (-not (Test-GhidraMcpUp) -and (Get-Date) -lt $deadline) { Start-Sleep 1 }
            Write-Host ("Ghidra: opened $url via GhidraGo (READ-ONLY, see comment in script); GhidraMCP on 8080: " + $(if (Test-GhidraMcpUp) { 'up' } else { 'NOT up' }))
        }
    }
}

if (-not $NoCheatEngine) {
    if (-not (Get-Process cheatengine* -ErrorAction SilentlyContinue)) { Start-Process $ceExe }
    $deadline = (Get-Date).AddSeconds(45)
    while (-not (Test-Path '\\.\pipe\CE_MCP_Bridge_v99') -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    Write-Host ('Cheat Engine MCP pipe: ' + $(if (Test-Path '\\.\pipe\CE_MCP_Bridge_v99') { 'up' } else { 'NOT up - is ce_mcp_bridge.lua in CE autorun?' }))
    Write-Host 'Attach from Claude with: evaluate_lua  openProcess("HomeworldRM.exe")'
}
