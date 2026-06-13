# Build a configured WinTune tree. Uses single-config flags for Ninja (ARM64 fallback).
#
# Usage:
#   .\scripts\cmake-build.ps1 -BuildDir build -Config Release
#   .\scripts\cmake-build.ps1 -BuildDir build-arm64 -Config Release

param(
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$Target = ""
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$BuildPath = Join-Path $Root $BuildDir
$marker = Join-Path $BuildPath "WINTUNE_CMAKE_GENERATOR.txt"

$args = @("--build", $BuildPath)
if (-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne "Ninja") {
    $args += @("--config", $Config)
}
if (-not [string]::IsNullOrWhiteSpace($Target)) {
    $args += @("--target", $Target)
}

Write-Host "cmake $($args -join ' ')"
& cmake @args
exit $LASTEXITCODE
