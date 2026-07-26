#!/usr/bin/env bash
# Shared helper: invoke a WinTune *.ps1 from Git Bash, MSYS, Cygwin, or WSL.
# Usage (sourced or via wrappers):
#   scripts/_invoke-ps1.sh build.ps1 [args...]
#
# WinTune's build system is PowerShell + MSVC on Windows. These wrappers exist
# so developers can stay in Bash without running .ps1 files as shell scripts.

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <script.ps1> [args...]" >&2
  exit 2
fi

SCRIPT_NAME="$1"
shift

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS1_UNIX="$ROOT/scripts/$SCRIPT_NAME"

if [[ ! -f "$PS1_UNIX" ]]; then
  echo "wintune: missing PowerShell script: $PS1_UNIX" >&2
  exit 1
fi

# Convert a Unix path to a Windows path PowerShell understands.
to_win_path() {
  local p="$1"
  if command -v wslpath >/dev/null 2>&1 && grep -qi microsoft /proc/version 2>/dev/null; then
    wslpath -w "$p"
    return
  fi
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$p"
    return
  fi
  # Git Bash / MSYS often expose /c/... — map manually as a last resort.
  if [[ "$p" =~ ^/([a-zA-Z])/(.*)$ ]]; then
    echo "${BASH_REMATCH[1]^}:/${BASH_REMATCH[2]//\//\\}"
    return
  fi
  echo "$p"
}

find_powershell() {
  if command -v pwsh.exe >/dev/null 2>&1; then
    command -v pwsh.exe
    return
  fi
  if command -v powershell.exe >/dev/null 2>&1; then
    command -v powershell.exe
    return
  fi
  # WSL: Windows System32 is usually on PATH via interop.
  local candidates=(
    "/mnt/c/Program Files/PowerShell/7/pwsh.exe"
    "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    "/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -x "$c" ]]; then
      echo "$c"
      return
    fi
  done
  return 1
}

PS="$(find_powershell)" || {
  echo "wintune: powershell.exe / pwsh.exe not found." >&2
  echo "Install PowerShell, or run the matching .ps1 from Windows PowerShell / CMD." >&2
  echo "WSL users: enable Windows interop, or open PowerShell outside WSL." >&2
  exit 1
}

PS1_WIN="$(to_win_path "$PS1_UNIX")"

# MSYS may rewrite arguments that look like paths; disable that for -File.
if [[ -n "${MSYSTEM:-}" ]]; then
  export MSYS2_ARG_CONV_EXCL='*'
fi

exec "$PS" -NoProfile -ExecutionPolicy Bypass -File "$PS1_WIN" "$@"
