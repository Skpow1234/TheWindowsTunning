# Build optional Inno Setup installer from a staged portable package (Phase 53).
#
# Portable ZIP remains the primary artifact. This script produces a Setup.exe
# when Inno Setup (iscc) is available.
#
# Usage:
#   .\scripts\package.ps1 -Config Release -Arch x64 -Version 0.2.0 -Zip
#   .\scripts\installer.ps1 -Version 0.2.0 -Arch x64
#   .\scripts\installer.ps1 -Version 0.2.0 -Arch x64 -PackageFirst -SignIfConfigured

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",
    [ValidateSet("stable", "beta")]
    [string]$Channel = "stable",
    [switch]$PackageFirst,
    [switch]$SignIfConfigured,
    [switch]$SkipIfMissingInno
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$Version = $Version.Trim()
if ($Version.StartsWith("v") -or $Version.StartsWith("V")) {
    $Version = $Version.Substring(1)
}

function Find-Iscc {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($null -ne $cmd) { return $cmd.Source }

    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe"
    )
    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

# Inno VersionInfoVersion must be numeric a.b.c.d — strip prerelease suffix.
function Get-NumericVersion {
    param([string]$Ver)
    if ($Ver -match '^(\d+\.\d+\.\d+)') {
        return "$($Matches[1]).0"
    }
    return "0.0.0.0"
}

if ($PackageFirst) {
    $pkgArgs = @{
        Config    = "Release"
        Arch      = $Arch
        Version   = $Version
        Channel   = $Channel
        Configure = $true
        Build     = $true
        Zip       = $true
    }
    if ($SignIfConfigured) { $pkgArgs.SignIfConfigured = $true }
    & (Join-Path $PSScriptRoot "package.ps1") @pkgArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$StageDir = Join-Path $Root "dist\WinTune-$Version-win-$Arch"
$StagedExe = Join-Path $StageDir "wintune.exe"
if (-not (Test-Path -LiteralPath $StagedExe)) {
    Write-Error "Stage missing: $StageDir (run package.ps1 first or pass -PackageFirst)"
    exit 1
}

$Iscc = Find-Iscc
if ($null -eq $Iscc) {
    if ($SkipIfMissingInno) {
        Write-Host "Inno Setup (iscc) not found — skipping installer (portable ZIP is primary)."
        exit 0
    }
    Write-Error "Inno Setup not found. Install from https://jrsoftware.org/isinfo.php or pass -SkipIfMissingInno."
    exit 1
}

$Iss = Join-Path $PSScriptRoot "installer\wintune.iss"
if (-not (Test-Path -LiteralPath $Iss)) {
    Write-Error "Missing ISS script: $Iss"
    exit 1
}

$NumericVer = Get-NumericVersion -Ver $Version
Write-Host "Building installer with $Iscc ..."
Write-Host "  Version=$Version  Arch=$Arch  VersionInfo=$NumericVer"

& $Iscc `
    "/DMyAppVersion=$Version" `
    "/DMyAppArch=$Arch" `
    "/DMyAppVersionInfo=$NumericVer" `
    $Iss
if ($LASTEXITCODE -ne 0) {
    Write-Error "iscc failed (exit $LASTEXITCODE)"
    exit 1
}

$SetupName = "WinTune-$Version-win-$Arch-setup.exe"
$SetupPath = Join-Path $Root "dist\$SetupName"
if (-not (Test-Path -LiteralPath $SetupPath)) {
    Write-Error "Expected installer not found: $SetupPath"
    exit 1
}

if ($SignIfConfigured) {
    & (Join-Path $PSScriptRoot "sign.ps1") -Path $SetupPath -SignIfConfigured -Verify -AllowUnsigned
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$Hash = (Get-FileHash -LiteralPath $SetupPath -Algorithm SHA256).Hash.ToLowerInvariant()
$ChecksumPath = "$SetupPath.sha256"
Set-Content -LiteralPath $ChecksumPath -Value "$Hash  $SetupName" -Encoding utf8

Write-Host "Installer: $SetupPath"
Write-Host "SHA-256:   $ChecksumPath"
exit 0
