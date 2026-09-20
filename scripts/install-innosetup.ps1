# Install Inno Setup 6 when missing (optional CI helper).
# Never fails the workflow hard: missing tools soft-skip so portable ZIP remains primary.
#
# Usage:
#   .\scripts\install-innosetup.ps1
#   .\scripts\install-innosetup.ps1 -Strict   # non-zero if install fails

param(
    [switch]$Strict
)

$ErrorActionPreference = "Continue"

function Find-Iscc {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($null -ne $cmd) { return $cmd.Source }

    foreach ($p in @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
            "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe"
        )) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

$existing = Find-Iscc
if ($null -ne $existing) {
    Write-Host "Inno Setup already available: $existing"
    exit 0
}

$wingetCmd = $null
$winget = Get-Command winget.exe -ErrorAction SilentlyContinue
if ($null -ne $winget) {
    $wingetCmd = $winget.Source
} else {
    $wingetCandidates = @(
        "$env:LOCALAPPDATA\Microsoft\WindowsApps\winget.exe",
        "$env:ProgramFiles\WindowsApps\Microsoft.DesktopAppInstaller*\winget.exe"
    )
    foreach ($pattern in $wingetCandidates) {
        $hit = Get-Item -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $hit) {
            $wingetCmd = $hit.FullName
            break
        }
    }
}

if ($null -ne $wingetCmd) {
    Write-Host "Installing Inno Setup via winget ($wingetCmd) ..."
    & $wingetCmd install --id JRSoftware.InnoSetup -e --disable-interactivity `
        --accept-source-agreements --accept-package-agreements
    $existing = Find-Iscc
    if ($null -ne $existing) {
        Write-Host "Inno Setup ready: $existing"
        exit 0
    }
    Write-Warning "winget finished but ISCC.exe not found; trying direct download."
} else {
    Write-Host "winget not available; downloading Inno Setup installer..."
}

try {
    $installer = Join-Path $env:TEMP ("innosetup-" + [guid]::NewGuid().ToString("n") + ".exe")
    # Official redirect to latest Inno Setup 6 Unicode installer.
    $url = "https://jrsoftware.org/download.php/is.exe"
    Write-Host "Downloading $url ..."
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing
    Write-Host "Running silent install..."
    $p = Start-Process -FilePath $installer -ArgumentList @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-"
    ) -Wait -PassThru
    Remove-Item -LiteralPath $installer -Force -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) {
        Write-Warning "Inno Setup installer exit code $($p.ExitCode)"
    }
} catch {
    Write-Warning "Direct Inno Setup install failed: $($_.Exception.Message)"
}

$existing = Find-Iscc
if ($null -ne $existing) {
    Write-Host "Inno Setup ready: $existing"
    exit 0
}

Write-Warning "Inno Setup not installed — Setup.exe build will soft-skip (ZIP remains primary)."
if ($Strict) { exit 1 }
exit 0
