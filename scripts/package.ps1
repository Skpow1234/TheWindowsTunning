# Install WinTune to dist/ and optionally create a portable ZIP.
#
# Usage:
#   .\scripts\package.ps1
#   .\scripts\package.ps1 -Config Release -Arch x64 -Zip
#   .\scripts\package.ps1 -Config Release -Arch arm64 -BuildDir build-arm64 -Zip -Version 0.1.2

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",
    [string]$BuildDir = "",
    [string]$Version = "",
    [ValidateSet("stable", "beta")]
    [string]$Channel = "stable",
    [switch]$Configure,
    [switch]$Build,
    [switch]$Zip,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = if ($Arch -eq "arm64") { "build-arm64" } else { "build" }
}

$BuildPath = Join-Path $Root $BuildDir
$DistDir = Join-Path $Root "dist"

function Invoke-CMake {
    param([string[]]$CMakeArgs)
    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed (exit $LASTEXITCODE): cmake $($CMakeArgs -join ' ')"
    }
}

function Get-CMakeCacheValue {
    param([string]$Key, [string]$Dir)
    $cache = Join-Path $Dir "CMakeCache.txt"
    if (-not (Test-Path $cache)) { return $null }
    $line = Select-String -Path $cache -Pattern "^${Key}:" | Select-Object -First 1
    if ($null -eq $line) { return $null }
    $parts = ($line.Line -split "=", 2)
    if ($parts.Count -lt 2) { return $null }
    return $parts[1].Trim()
}

if ($Configure -or -not (Test-Path (Join-Path $BuildPath "CMakeCache.txt"))) {
    $defines = @("-DWINTUNE_WARNINGS_AS_ERRORS=ON")
    if (-not [string]::IsNullOrWhiteSpace($Version)) {
        $defines += "-DWINTUNE_VERSION=$Version"
    }
    Write-Host "Configuring $BuildDir ($Arch) ..."
    & (Join-Path $PSScriptRoot "cmake-configure.ps1") -BuildDir $BuildDir -Arch $Arch `
        -Clean:$Configure -DefineArg $defines
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Get-CMakeCacheValue -Key "WINTUNE_VERSION" -Dir $BuildPath
}
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = "0.0.0-dev"
}

if ($Build -and -not $SkipBuild) {
    Write-Host "Building wintune ($Config, $Arch) ..."
    Invoke-CMake @("--build", $BuildPath, "--config", $Config)
}

$Exe = Join-Path $BuildPath "$Config\wintune.exe"
if (-not (Test-Path $Exe)) {
    Write-Error "Executable not found: $Exe (run with -Build or build first)"
    exit 1
}

$StageName = "WinTune-$Version-win-$Arch"
$StageDir = Join-Path $DistDir $StageName

if (Test-Path $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Write-Host "Installing to $StageDir ..."
Invoke-CMake @("--install", $BuildPath, "--config", $Config, "--prefix", $StageDir)

$PackDir = Join-Path $Root "pack"
foreach ($File in @("QUICKSTART.txt", "Run-Doctor.cmd", "Run-Help.cmd",
                   "Launch-WinTune.cmd", "Launch-WinTune.ps1")) {
    $Src = Join-Path $PackDir $File
    if (Test-Path $Src) {
        Copy-Item -LiteralPath $Src -Destination (Join-Path $StageDir $File) -Force
    }
}

Set-Content -LiteralPath (Join-Path $StageDir "VERSION.txt") -Value $Version `
    -NoNewline -Encoding utf8
Set-Content -LiteralPath (Join-Path $StageDir "CHANNEL.txt") -Value $Channel `
    -NoNewline -Encoding utf8

Write-Host ""
Write-Host "Package directory: $StageDir"
$versionOut = & $Exe version 2>&1 | Out-String
Write-Host $versionOut.TrimEnd()
if ($versionOut -match "Arch:\s+(\S+)") {
    $reported = $Matches[1].ToLowerInvariant()
    if ($reported -ne $Arch) {
        Write-Error "Architecture mismatch: requested '$Arch' but binary reports '$reported'."
        exit 1
    }
}

if ($Zip) {
    if (-not (Test-Path $DistDir)) {
        New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
    }
    $ZipName = "$StageName.zip"
    $ZipPath = Join-Path $DistDir $ZipName
    $ChecksumPath = "$ZipPath.sha256"
    if (Test-Path $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    Write-Host "Creating $ZipPath ..."
    $ArchiveItems = Get-ChildItem -LiteralPath $StageDir | ForEach-Object { $_.FullName }
    Compress-Archive -Path $ArchiveItems -DestinationPath $ZipPath -Force
    $Hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath $ChecksumPath -Value "$Hash  $ZipName" -Encoding utf8
    Write-Host "ZIP:      $ZipPath"
    Write-Host "SHA-256:  $ChecksumPath"
}

exit 0
