@echo off
rem HW2 Frame VR - removes the mod and restores anything it backed up (opengl32.dll, HomeworldRM.exe).
rem Settings (hw2vr.ini) and the log are kept; add -Purge to remove them too.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\uninstall.ps1" %*
echo.
pause
