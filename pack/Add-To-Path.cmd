@echo off
REM Add this folder to your user PATH so `wintune` works from any terminal.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Add-To-Path.ps1" %*
pause
