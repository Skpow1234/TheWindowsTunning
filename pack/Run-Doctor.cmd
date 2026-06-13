@echo off
setlocal
cd /d "%~dp0"
title WinTune Doctor
echo Running WinTune doctor (scan + recommendations)...
echo.
wintune.exe doctor
set ERR=%ERRORLEVEL%
echo.
if %ERR% NEQ 0 (
    echo WinTune exited with code %ERR%.
) else (
    echo Done.
)
echo.
pause
exit /b %ERR%
