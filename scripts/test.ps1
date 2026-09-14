# Run unit tests when the CMake test target is configured.
# For a full CLI smoke pass over every command, see scripts/smoke.ps1
# and docs/testing.md.

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $Root $BuildDir
Set-Location $Root

if (-not (Test-Path (Join-Path $BuildPath "CMakeCache.txt"))) {
    Write-Error "Build directory not configured ($BuildDir). Run .\scripts\build.ps1 or cmake-configure first."
    exit 1
}

Write-Host "Building test targets ($Config, $BuildDir) ..."
& cmake --build $BuildPath --config $Config --target wintune_tests_all
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build unit test targets."
    exit $LASTEXITCODE
}

Write-Host "Running ctest ..."
& ctest --test-dir $BuildPath -C $Config --output-on-failure
exit $LASTEXITCODE
