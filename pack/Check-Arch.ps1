# Returns 0 if ARCH.txt matches this PC (or ARCH.txt is missing).
# Returns 1 on mismatch and prints a clear message.
# Dot-source or call: .\Check-Arch.ps1 ; if ($LASTEXITCODE -ne 0) { exit 1 }

param(
    [string]$Folder = $PSScriptRoot
)

$ErrorActionPreference = "Stop"
$archFile = Join-Path $Folder "ARCH.txt"
if (-not (Test-Path -LiteralPath $archFile)) {
    exit 0
}

$package = (Get-Content -LiteralPath $archFile -Raw -ErrorAction SilentlyContinue)
if ([string]::IsNullOrWhiteSpace($package)) {
    exit 0
}
$package = $package.Trim().ToLowerInvariant()

$hostArch = $env:PROCESSOR_ARCHITECTURE
$native = switch -Regex ($hostArch) {
    'ARM64' { 'arm64' }
    'AMD64' { 'x64' }
    'x86' {
        # WoW64 on ARM64 reports x86 for 32-bit hosts; prefer PROCESSOR_IDENTIFIER / env
        if ($env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { 'arm64' }
        elseif ($env:PROCESSOR_ARCHITEW6432 -eq 'AMD64') { 'x64' }
        else { 'x86' }
    }
    default { $hostArch.ToLowerInvariant() }
}

if ($package -eq $native) {
    exit 0
}

Write-Host ""
Write-Host "Wrong architecture for this PC." -ForegroundColor Red
Write-Host "  This folder is packaged for:  $package" -ForegroundColor Yellow
Write-Host "  This PC reports:              $native" -ForegroundColor Yellow
Write-Host ""
Write-Host "Download the matching ZIP from GitHub Releases:" -ForegroundColor Cyan
if ($native -eq 'arm64') {
    Write-Host "  WinTune-*-win-arm64.zip"
} else {
    Write-Host "  WinTune-*-win-x64.zip   (most Intel/AMD PCs)"
}
Write-Host ""
Write-Host "Running the wrong wintune.exe usually shows:"
Write-Host "  ""This app can't run on your PC"""
Write-Host ""
exit 1
