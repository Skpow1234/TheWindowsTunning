# Install built binaries to dist/ (local packaging).

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build"
$Prefix = Join-Path $Root "dist"
Set-Location $Root

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Error "Build directory not configured. Run .\scripts\build.ps1 first."
    exit 1
}

Write-Host "Installing to $Prefix ($Config) ..."
& cmake --install $BuildDir --config $Config --prefix $Prefix
exit $LASTEXITCODE
