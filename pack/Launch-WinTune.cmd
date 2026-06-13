@echo off
setlocal
cd /d "%~dp0"
title WinTune
powershell -NoExit -ExecutionPolicy Bypass -File "%~dp0Launch-WinTune.ps1"
