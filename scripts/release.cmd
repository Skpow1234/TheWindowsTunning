@echo off
REM WinTune release wrapper for CMD and Git Bash (calls PowerShell).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0release.ps1" %*
exit /b %ERRORLEVEL%
