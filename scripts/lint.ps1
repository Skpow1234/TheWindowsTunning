# WinTune lint: MSVC /W4 with warnings treated as errors.
# Usage: .\scripts\lint.ps1

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build-lint"

Set-Location $Root

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed (exit $LASTEXITCODE): cmake $($CMakeArgs -join ' ')"
    }
}

Write-Host "Configuring WinTune lint build (warnings as errors) ..."
& (Join-Path $PSScriptRoot "cmake-configure.ps1") -BuildDir "build-lint" -Clean `
    -DefineArg "-DWINTUNE_WARNINGS_AS_ERRORS=ON"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building wintune ($Config) with /WX ..."
Invoke-CMake @("--build", $BuildDir, "--config", $Config)

Write-Host ""
Write-Host "Lint OK: no warnings under /W4 /WX."
