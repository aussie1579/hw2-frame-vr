@echo off
rem HW2 Frame VR - starts Steam and SteamVR if needed, then Homeworld 2 Remastered straight into VR.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch-vr.ps1" %*
if errorlevel 1 pause
