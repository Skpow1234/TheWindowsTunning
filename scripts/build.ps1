# WinTune build script (Windows PowerShell).
# Usage:
#   .\scripts\build.ps1              # Debug (default)
#   .\scripts\build.ps1 -Config Release
#   .\scripts\build.ps1 -Reconfigure # wipe build/ and reconfigure
#
# From Git Bash or CMD, use scripts\build.cmd instead.

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build"

Set-Location $Root

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed (exit $LASTEXITCODE): cmake $($CMakeArgs -join ' ')"
    }
}

if ($Reconfigure -and (Test-Path $BuildDir)) {
    Write-Host "Removing $BuildDir ..."
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$CacheFile = Join-Path $BuildDir "CMakeCache.txt"

if (-not (Test-Path $CacheFile)) {
    Write-Host "Configuring WinTune (first-time or after clean) ..."
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

    try {
        Invoke-CMake @("-S", ".", "-B", "build", "-G", "Visual Studio 17 2022", "-A", "x64")
    } catch {
        Write-Warning "Visual Studio 17 2022 generator failed; trying CMake default."
        Invoke-CMake @("-S", ".", "-B", "build")
    }
}

Write-Host "Building wintune ($Config) ..."
Invoke-CMake @("--build", "build", "--config", $Config)

$Exe = Join-Path $BuildDir "$Config\wintune.exe"
if (Test-Path $Exe) {
    Write-Host ""
    Write-Host "OK: $Exe"
    Write-Host "Run:  .\build\$Config\wintune.exe help"
} else {
    Write-Warning "Build finished but executable not found at $Exe"
}

exit 0
