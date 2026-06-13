# Configure a WinTune CMake build directory with a Visual Studio generator when
# available. GitHub Actions windows-latest (June 2026+) ships VS 2026, not VS 2022.
#
# Usage:
#   .\scripts\cmake-configure.ps1 -BuildDir build -DefineArg "-DWINTUNE_WARNINGS_AS_ERRORS=ON"
#   .\scripts\cmake-configure.ps1 -BuildDir build -DefineArg "-DWINTUNE_VERSION=0.1.0" -DefineArg "-DWINTUNE_WARNINGS_AS_ERRORS=ON"

param(
    [string]$BuildDir = "build",
    [switch]$Clean,
    [string[]]$DefineArg = @()
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

$BuildPath = Join-Path $Root $BuildDir
$CacheFile = Join-Path $BuildPath "CMakeCache.txt"

if ($Clean -and (Test-Path $BuildPath)) {
    Remove-Item -LiteralPath $BuildPath -Recurse -Force
}

if (-not $Clean -and (Test-Path $CacheFile)) {
    Write-Host "CMake cache exists: $CacheFile (skipping configure)"
    exit 0
}

New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null

function Invoke-Configure {
    param([string[]]$CMakeArgs)
    Write-Host "cmake $($CMakeArgs -join ' ')"
    & cmake @CMakeArgs
    return $LASTEXITCODE
}

$generators = @(
    @{ Name = "Visual Studio 18 2026"; Arch = "x64" },
    @{ Name = "Visual Studio 17 2022"; Arch = "x64" }
)

foreach ($gen in $generators) {
    $args = @(
        "-S", ".", "-B", $BuildDir,
        "-G", $gen.Name, "-A", $gen.Arch
    ) + $DefineArg

    if ((Invoke-Configure $args) -eq 0) {
        Write-Host "Configured with $($gen.Name)"
        exit 0
    }

    if (Test-Path $BuildPath) {
        Remove-Item -LiteralPath $BuildPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
}

Write-Warning "No Visual Studio generator matched; using CMake default."
$args = @("-S", ".", "-B", $BuildDir) + $DefineArg
if ((Invoke-Configure $args) -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Configured with CMake default generator"
exit 0
