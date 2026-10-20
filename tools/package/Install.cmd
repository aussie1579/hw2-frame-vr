@echo off
rem HW2 Frame VR - installs into Homeworld Remastered (HomeworldRM\Bin\Release, found through Steam).
rem Pass -GameDir "<...\HomeworldRM\Bin\Release>" if the game isn't in a Steam library.
echo HW2 Frame VR - install
echo.
echo The VR window is 2880x2880, more than the 32-bit game can draw in its default 2 GB of memory.
echo The installer can set the Large Address Aware flag in HomeworldRM.exe (one bit) so it gets 4 GB.
echo The original exe is backed up and Uninstall.cmd puts it back. A Steam update or file check
echo undoes the patch - run Install.cmd again then.
echo.
choice /c YN /m "Patch HomeworldRM.exe to be Large Address Aware (needed for Launch VR.cmd)"
if errorlevel 2 (set HW2VR_LAA=) else (set HW2VR_LAA=-LargeAddressAware)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install.ps1" %HW2VR_LAA% %*
set HW2VR_LAA=
echo.
pause
