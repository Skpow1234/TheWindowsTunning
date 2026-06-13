# Package a portable WinTune release ZIP (and SHA-256 checksum).
# Usage:
#   .\scripts\release.ps1 -Version 0.1.0
#   .\scripts\release.ps1 -Version v0.1.0 -Config Release

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build"
$DistDir = Join-Path $Root "dist"
$StagingDir = Join-Path $DistDir "staging"

$Version = $Version.Trim()
if ($Version.StartsWith("v") -or $Version.StartsWith("V")) {
    $Version = $Version.Substring(1)
}

Set-Location $Root

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed (exit $LASTEXITCODE): cmake $($CMakeArgs -join ' ')"
    }
}

if (-not $SkipBuild) {
    Write-Host "Configuring WinTune release build ($Version) ..."
    & (Join-Path $PSScriptRoot "cmake-configure.ps1") -BuildDir "build" -Clean `
        -DefineArg "-DWINTUNE_VERSION=$Version" `
        -DefineArg "-DWINTUNE_WARNINGS_AS_ERRORS=ON"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Building wintune ($Config) ..."
    Invoke-CMake @("--build", "build", "--config", $Config)
}

$Exe = Join-Path $BuildDir "$Config\wintune.exe"
if (-not (Test-Path $Exe)) {
    Write-Error "Executable not found: $Exe"
    exit 1
}

if (Test-Path $StagingDir) {
    Remove-Item -LiteralPath $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

Copy-Item -LiteralPath $Exe -Destination (Join-Path $StagingDir "wintune.exe")
foreach ($File in @("README.md", "LICENSE")) {
    $Src = Join-Path $Root $File
    if (Test-Path $Src) {
        Copy-Item -LiteralPath $Src -Destination (Join-Path $StagingDir $File)
    }
}

$VersionFile = Join-Path $StagingDir "VERSION.txt"
Set-Content -LiteralPath $VersionFile -Value $Version -NoNewline -Encoding utf8

$ZipName = "WinTune-$Version-win-x64.zip"
$ZipPath = Join-Path $DistDir $ZipName
$ChecksumPath = "$ZipPath.sha256"

if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}
if (Test-Path $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}

Write-Host "Creating $ZipPath ..."
$ArchiveItems = Get-ChildItem -LiteralPath $StagingDir | ForEach-Object { $_.FullName }
Compress-Archive -Path $ArchiveItems -DestinationPath $ZipPath -Force

$Hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $ChecksumPath -Value "$Hash  $ZipName" -Encoding utf8

Write-Host ""
Write-Host "Release package: $ZipPath"
Write-Host "SHA-256:         $ChecksumPath"
Write-Host "Version check:   $Version"

$Reported = & $Exe version 2>&1 | Out-String
Write-Host $Reported.TrimEnd()

if ($Reported -notmatch [regex]::Escape($Version)) {
    Write-Warning "Built binary version output does not contain $Version"
}

Remove-Item -LiteralPath $StagingDir -Recurse -Force

exit 0
