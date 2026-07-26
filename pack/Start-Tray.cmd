@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Arch.ps1" -Folder "%~dp0"
if errorlevel 1 (
    pause
    exit /b 1
)
start "" "%~dp0wintune.exe" tray
