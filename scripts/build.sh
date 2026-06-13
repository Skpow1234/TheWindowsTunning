#!/usr/bin/env bash
# WinTune build wrapper for Git Bash / MSYS (calls PowerShell).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ROOT/scripts/build.ps1" "$@"
exit $?
