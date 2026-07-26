@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0lint.ps1" %*
exit /b %ERRORLEVEL%
