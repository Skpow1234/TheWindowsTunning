# Run tests when the CMake test target is configured.

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build"
Set-Location $Root

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Error "Build directory not configured. Run .\scripts\build.ps1 first."
    exit 1
}

Write-Host "Building test targets ($Config) ..."
& cmake --build $BuildDir --config $Config --target wintune_unit_tests
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build wintune_unit_tests."
    exit $LASTEXITCODE
}

Write-Host "Running ctest ..."
& ctest --test-dir $BuildDir -C $Config --output-on-failure
exit $LASTEXITCODE
