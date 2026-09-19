# Authenticode sign and/or verify WinTune binaries (Phase 52).
#
# Never commits certificates. Signing runs only when credentials are present
# (or -Sign is forced with an explicit PFX path).
#
# Usage:
#   .\scripts\sign.ps1 -Path .\dist\...\wintune.exe -Verify -AllowUnsigned
#   .\scripts\sign.ps1 -Path .\dist\...\wintune.exe -SignIfConfigured
#   .\scripts\sign.ps1 -Path .\wintune.exe -Sign -PfxPath cert.pfx
#
# Env (optional, for CI):
#   WINTUNE_SIGN_PFX_BASE64  — base64-encoded .pfx
#   WINTUNE_SIGN_PASSWORD    — PFX password
#   WINTUNE_SIGN_TIMESTAMP   — timestamp URL (default DigiCert)
#   WINTUNE_SIGN_ENABLED     — "true" to require SignIfConfigured to sign

param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [switch]$Sign,
    [switch]$SignIfConfigured,
    [switch]$Verify,
    [switch]$AllowUnsigned,
    [switch]$RequireSigned,

    [string]$PfxPath = "",
    [string]$Password = "",
    [string]$TimestampUrl = "",
    [string]$Description = "WinTune"
)

$ErrorActionPreference = "Stop"

function Find-SignTool {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($null -ne $cmd) { return $cmd.Source }

    $kitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kitsRoot) {
        $candidates = Get-ChildItem -LiteralPath $kitsRoot -Filter "signtool.exe" -Recurse `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending
        if ($candidates.Count -gt 0) { return $candidates[0].FullName }
    }
    return $null
}

function Test-IsAuthenticodeSigned {
    param([string]$FilePath)
    try {
        $sig = Get-AuthenticodeSignature -FilePath $FilePath
        return ($sig.Status -eq "Valid")
    } catch {
        return $false
    }
}

function Write-SigningStatusFile {
    param(
        [string]$ExePath,
        [string]$Status
    )
    $dir = Split-Path -Parent $ExePath
    if ([string]::IsNullOrWhiteSpace($dir)) { return }
    $out = Join-Path $dir "SIGNING.txt"
    Set-Content -LiteralPath $out -Value $Status -NoNewline -Encoding utf8
}

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Error "File not found: $Path"
    exit 1
}

$Path = (Resolve-Path -LiteralPath $Path).Path
$SignTool = Find-SignTool

if ([string]::IsNullOrWhiteSpace($TimestampUrl)) {
    if (-not [string]::IsNullOrWhiteSpace($env:WINTUNE_SIGN_TIMESTAMP)) {
        $TimestampUrl = $env:WINTUNE_SIGN_TIMESTAMP
    } else {
        $TimestampUrl = "http://timestamp.digicert.com"
    }
}

$doSign = $false
if ($Sign) {
    $doSign = $true
} elseif ($SignIfConfigured) {
    $enabled = ($env:WINTUNE_SIGN_ENABLED -eq "true" -or
                $env:WINTUNE_SIGN_ENABLED -eq "1")
    $hasPfxEnv = -not [string]::IsNullOrWhiteSpace($env:WINTUNE_SIGN_PFX_BASE64)
    $hasPfxPath = -not [string]::IsNullOrWhiteSpace($PfxPath) -and (Test-Path -LiteralPath $PfxPath)
    if ($enabled -or $hasPfxEnv -or $hasPfxPath) {
        $doSign = $true
    } else {
        Write-Host "Signing skipped (no WINTUNE_SIGN_* credentials / -PfxPath)."
        Write-SigningStatusFile -ExePath $Path -Status "unsigned"
    }
}

if ($doSign) {
    if ($null -eq $SignTool) {
        Write-Error "signtool.exe not found. Install Windows SDK Signing Tools."
        exit 1
    }

    $tempPfx = $null
    try {
        if ([string]::IsNullOrWhiteSpace($PfxPath)) {
            if ([string]::IsNullOrWhiteSpace($env:WINTUNE_SIGN_PFX_BASE64)) {
                Write-Error "Signing requested but no -PfxPath or WINTUNE_SIGN_PFX_BASE64."
                exit 1
            }
            $bytes = [Convert]::FromBase64String($env:WINTUNE_SIGN_PFX_BASE64)
            $tempPfx = Join-Path $env:TEMP ("wintune-sign-" + [guid]::NewGuid().ToString("n") + ".pfx")
            [IO.File]::WriteAllBytes($tempPfx, $bytes)
            $PfxPath = $tempPfx
        }

        if ([string]::IsNullOrWhiteSpace($Password)) {
            $Password = $env:WINTUNE_SIGN_PASSWORD
        }
        if ($null -eq $Password) { $Password = "" }

        Write-Host "Signing $Path ..."
        $signArgs = @(
            "sign",
            "/fd", "SHA256",
            "/td", "SHA256",
            "/tr", $TimestampUrl,
            "/f", $PfxPath,
            "/d", $Description
        )
        if (-not [string]::IsNullOrWhiteSpace($Password)) {
            $signArgs += @("/p", $Password)
        }
        $signArgs += $Path

        & $SignTool @signArgs
        if ($LASTEXITCODE -ne 0) {
            throw "signtool sign failed (exit $LASTEXITCODE)"
        }

        Write-SigningStatusFile -ExePath $Path -Status "signed"
        Write-Host "Signed OK."
        $Verify = $true
        $RequireSigned = $true
        $AllowUnsigned = $false
    } finally {
        if ($null -ne $tempPfx -and (Test-Path -LiteralPath $tempPfx)) {
            Remove-Item -LiteralPath $tempPfx -Force -ErrorAction SilentlyContinue
        }
    }
}

if ($Verify -or $RequireSigned) {
    $valid = Test-IsAuthenticodeSigned -FilePath $Path
    if ($valid) {
        Write-Host "Authenticode verify OK: $Path"
        if ($null -ne $SignTool) {
            & $SignTool verify /pa $Path
            if ($LASTEXITCODE -ne 0) {
                Write-Error "signtool verify failed (exit $LASTEXITCODE)"
                exit 1
            }
        }
        if (-not $doSign) {
            Write-SigningStatusFile -ExePath $Path -Status "signed"
        }
        exit 0
    }

    if ($RequireSigned) {
        Write-Error "Authenticode signature required but missing/invalid: $Path"
        exit 1
    }
    if ($AllowUnsigned) {
        Write-Host "Authenticode not present (allowed): $Path"
        Write-SigningStatusFile -ExePath $Path -Status "unsigned"
        exit 0
    }

    Write-Error "Authenticode signature missing/invalid: $Path (pass -AllowUnsigned to soft-pass)"
    exit 1
}

exit 0
