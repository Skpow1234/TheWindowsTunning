@echo off
REM WinTune build wrapper for CMD and Git Bash (calls PowerShell).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
