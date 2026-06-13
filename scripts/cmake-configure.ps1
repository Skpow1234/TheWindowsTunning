# Configure a WinTune CMake build directory with a Visual Studio generator when
# available. GitHub Actions windows-latest (June 2026+) ships VS 2026, not VS 2022.
# Native ARM64 builds should use the windows-11-vs2026-arm runner (see docs/ci.md).
#
# Usage:
#   .\scripts\cmake-configure.ps1 -BuildDir build
#   .\scripts\cmake-configure.ps1 -BuildDir build-arm64 -Arch arm64
#   .\scripts\cmake-configure.ps1 -BuildDir build -DefineArg "-DWINTUNE_WARNINGS_AS_ERRORS=ON"

param(
    [string]$BuildDir = "build",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",
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

$CMakeArch = if ($Arch -eq "arm64") { "ARM64" } else { "x64" }
$HostArch = $env:PROCESSOR_ARCHITECTURE
$IsNativeArm64 = ($Arch -eq "arm64") -and ($HostArch -eq "ARM64")

function Get-VsWherePath {
    $p = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $p) { return $p }
    return $null
}

function Test-VsArm64Toolchain {
    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) { return $false }
    $found = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 `
        -property installationPath 2>$null
    return -not [string]::IsNullOrWhiteSpace($found)
}

function Get-CMakeVsGenerators {
    param([string]$TargetArch)

    $known = @(
        @{ Name = "Visual Studio 18 2026"; MinVersion = [version]"18.0" },
        @{ Name = "Visual Studio 17 2022"; MinVersion = [version]"17.0" }
    )

    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) {
        return @(
            @{ Name = "Visual Studio 18 2026"; Arch = $TargetArch },
            @{ Name = "Visual Studio 17 2022"; Arch = $TargetArch }
        )
    }

    $installs = @(& $vswhere -all -products * `
        -requires Microsoft.VisualStudio.Workload.VCTools `
        -property installationVersion 2>$null)

    if ($installs.Count -eq 0) {
        return @(
            @{ Name = "Visual Studio 18 2026"; Arch = $TargetArch },
            @{ Name = "Visual Studio 17 2022"; Arch = $TargetArch }
        )
    }

    $gens = New-Object System.Collections.Generic.List[object]
    foreach ($entry in $installs) {
        if ([string]::IsNullOrWhiteSpace($entry)) { continue }
        $ver = [version]($entry -split ";", 2)[0]
        foreach ($k in $known) {
            if ($ver.Major -eq $k.MinVersion.Major) {
                $gens.Add(@{ Name = $k.Name; Arch = $TargetArch })
                break
            }
        }
    }

    if ($gens.Count -eq 0) {
        return @(
            @{ Name = "Visual Studio 18 2026"; Arch = $TargetArch },
            @{ Name = "Visual Studio 17 2022"; Arch = $TargetArch }
        )
    }
    return $gens
}

function Invoke-Configure {
    param([string[]]$CMakeArgs)
    Write-Host "cmake $($CMakeArgs -join ' ')"
    & cmake @CMakeArgs
    return $LASTEXITCODE
}

if ($Arch -eq "arm64" -and -not $IsNativeArm64 -and -not (Test-VsArm64Toolchain)) {
    Write-Error @"
ARM64 cross-compile is not available on this x64 host.

Visual Studio is present but the MSVC ARM64 build tools workload is missing.
Options:
  1. CI: use runs-on: windows-11-vs2026-arm for native ARM64 builds.
  2. Local: install 'MSVC v143/v145 - VS 20xx C++ ARM64 build tools' in Visual Studio Installer.
  3. Local: build on a Windows ARM64 machine.

Re-run after installing tools:
  .\scripts\cmake-configure.ps1 -BuildDir build-arm64 -Arch arm64 -Clean
"@
    exit 1
}

$generators = Get-CMakeVsGenerators -TargetArch $CMakeArch

foreach ($gen in $generators) {
    $args = @(
        "-S", ".", "-B", $BuildDir,
        "-G", $gen.Name, "-A", $gen.Arch
    ) + $DefineArg

    if ((Invoke-Configure $args) -eq 0) {
        Write-Host "Configured with $($gen.Name) ($Arch, host=$HostArch)"
        exit 0
    }

    if (Test-Path $BuildPath) {
        Remove-Item -LiteralPath $BuildPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
}

if ($Arch -eq "arm64") {
    Write-Error @"
ARM64 configure failed. No working Visual Studio generator was found.

If this is GitHub Actions, set the ARM64 job to:
  runs-on: windows-11-vs2026-arm

Local x64 hosts need the ARM64 MSVC workload, or build on ARM64 Windows.
"@
    exit 1
}

Write-Warning "No Visual Studio generator matched; using CMake default."
$args = @("-S", ".", "-B", $BuildDir) + $DefineArg
if ((Invoke-Configure $args) -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Configured with CMake default generator ($Arch)"
exit 0
