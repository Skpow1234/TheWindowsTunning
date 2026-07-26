@echo off
setlocal
cd /d "%~dp0"
title WinTune Doctor
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Arch.ps1" -Folder "%~dp0"
if errorlevel 1 (
    pause
    exit /b 1
)
echo Running WinTune doctor (scan + recommendations)...
echo.
"%~dp0wintune.exe" doctor
set ERR=%ERRORLEVEL%
echo.
if %ERR% NEQ 0 (
    echo WinTune exited with code %ERR%.
    if %ERR% EQU 11 (
        echo Tip: some checks need an elevated shell — right-click PowerShell,
        echo "Run as administrator", then run: wintune doctor
    )
) else (
    echo Done. Tip: for a live view try  wintune tui  or  Start-Tray.cmd
)
echo.
pause
exit /b %ERR%
