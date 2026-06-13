# Package portable WinTune release ZIPs for x64 and ARM64 (+ SHA-256 checksums).
# Usage:
#   .\scripts\release.ps1 -Version 0.1.2
#   .\scripts\release.ps1 -Version v0.1.2 -Config Release -Arch x64

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [ValidateSet("x64", "arm64", "all")]
    [string]$Arch = "all",
    [ValidateSet("stable", "beta")]
    [string]$Channel = "stable",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$Version = $Version.Trim()
if ($Version.StartsWith("v") -or $Version.StartsWith("V")) {
    $Version = $Version.Substring(1)
}

$PackageScript = Join-Path $PSScriptRoot "package.ps1"
$archList = if ($Arch -eq "all") { @("x64", "arm64") } else { @($Arch) }

foreach ($a in $archList) {
    Write-Host ""
    Write-Host "=== Release package: $Version win-$a ===" -ForegroundColor Cyan
    $buildDir = if ($a -eq "arm64") { "build-arm64" } else { "build" }
    $args = @{
        Config    = $Config
        Arch      = $a
        BuildDir  = $buildDir
        Version   = $Version
        Channel   = $Channel
        Configure = $true
        Build     = (-not $SkipBuild)
        Zip       = $true
    }
    if ($SkipBuild) { $args.SkipBuild = $true }
    & $PackageScript @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host ""
Write-Host "Release artifacts in dist/" -ForegroundColor Green
exit 0
