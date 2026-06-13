# Remove CMake build and install output directories.

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

foreach ($dir in @("build", "dist")) {
    $path = Join-Path $Root $dir
    if (Test-Path $path) {
        Write-Host "Removing $path ..."
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}

Get-ChildItem -Path $Root -Directory -Filter "build-*" | ForEach-Object {
    Write-Host "Removing $($_.FullName) ..."
    Remove-Item -LiteralPath $_.FullName -Recurse -Force
}

Write-Host "Clean complete."
