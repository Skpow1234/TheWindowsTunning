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

Write-Host "cmake-configure: target=$Arch host=$HostArch native_arm64=$IsNativeArm64"
& cmake --version

function Get-VsWherePath {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )
    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

function Get-VsInstanceForMajor {
    param([int]$Major)

    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) { return $null }

    $range = "[$Major,$([int]($Major + 1)))"
    $path = & $vswhere -version $range -latest -products * -property installationPath 2>$null
    if ([string]::IsNullOrWhiteSpace($path)) { return $null }
    return $path.Trim()
}

function Test-VsArm64Toolchain {
    if ($IsNativeArm64) { return $true }
    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) { return $false }
    $found = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 `
        -property installationPath 2>$null
    return -not [string]::IsNullOrWhiteSpace($found)
}

function Get-InstalledVsGenerators {
    param([string]$TargetArch)

    $map = @{
        18 = "Visual Studio 18 2026"
        17 = "Visual Studio 17 2022"
    }

    $vswhere = Get-VsWherePath
    if ($null -eq $vswhere) {
        return @(
            @{ Name = "Visual Studio 18 2026"; Arch = $TargetArch },
            @{ Name = "Visual Studio 17 2022"; Arch = $TargetArch }
        )
    }

    $versions = @(& $vswhere -all -products * `
        -requires Microsoft.VisualStudio.Workload.VCTools `
        -property installationVersion 2>$null)

    if ($versions.Count -eq 0) {
        $versions = @(& $vswhere -all -products * -property installationVersion 2>$null)
    }

    $majors = New-Object System.Collections.Generic.List[int]
    foreach ($entry in $versions) {
        if ([string]::IsNullOrWhiteSpace($entry)) { continue }
        $verText = ($entry -split ";", 2)[0].Trim()
        try {
            $ver = [version]$verText
            if (-not $majors.Contains($ver.Major)) {
                [void]$majors.Add($ver.Major)
            }
        } catch {
            continue
        }
    }

    if ($majors.Count -eq 0) {
        return @(
            @{ Name = "Visual Studio 18 2026"; Arch = $TargetArch },
            @{ Name = "Visual Studio 17 2022"; Arch = $TargetArch }
        )
    }

    $gens = New-Object System.Collections.Generic.List[object]
    foreach ($major in ($majors | Sort-Object -Descending)) {
        if ($map.ContainsKey($major)) {
            $gens.Add(@{ Name = $map[$major]; Arch = $TargetArch })
        }
    }
    return $gens
}

function Get-ConfigureAttempts {
    param(
        [array]$Generators,
        [switch]$NativeArm64
    )

    $attempts = New-Object System.Collections.Generic.List[object]
    foreach ($gen in $Generators) {
        $major = 0
        if ($gen.Name -like "*18 2026*") { $major = 18 }
        elseif ($gen.Name -like "*17 2022*") { $major = 17 }

        $instance = $null
        if ($major -gt 0) {
            $instance = Get-VsInstanceForMajor -Major $major
        }
        if ($instance) {
            Write-Host "Visual Studio instance for $($gen.Name): $instance"
        }

        if ($NativeArm64) {
            if ($instance) {
                $attempts.Add(@{
                    Name     = $gen.Name
                    Arch     = $null
                    Instance = $instance
                    Label    = "$($gen.Name) (native host, default platform)"
                })
            }
            $attempts.Add(@{
                Name     = $gen.Name
                Arch     = $null
                Instance = $null
                Label    = "$($gen.Name) (native host)"
            })
        }

        if ($instance) {
            $attempts.Add(@{
                Name     = $gen.Name
                Arch     = $gen.Arch
                Instance = $instance
                Label    = "$($gen.Name) -A $($gen.Arch) (with instance)"
            })
        }
        $attempts.Add(@{
            Name     = $gen.Name
            Arch     = $gen.Arch
            Instance = $null
            Label    = "$($gen.Name) -A $($gen.Arch)"
        })
    }
    return $attempts
}

function Invoke-Configure {
    param(
        [string[]]$CMakeArgs,
        [string]$Label
    )
    Write-Host "Trying: $Label"
    Write-Host "cmake $($CMakeArgs -join ' ')"
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & cmake @CMakeArgs 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEap
    }
    if ($code -ne 0) {
        Write-Warning "Configure failed (exit $code):"
        $output | ForEach-Object { Write-Warning $_ }
    }
    return $code
}

function Try-NinjaNativeArm64 {
    param([string[]]$ExtraDefines)

    $instance = Get-VsInstanceForMajor -Major 18
    if ([string]::IsNullOrWhiteSpace($instance)) {
        $instance = Get-VsInstanceForMajor -Major 17
    }
    if ([string]::IsNullOrWhiteSpace($instance)) {
        return $false
    }

    $devShell = Join-Path $instance "Common7\Tools\Launch-VsDevShell.ps1"
    if (-not (Test-Path -LiteralPath $devShell)) {
        Write-Warning "Launch-VsDevShell.ps1 not found at $devShell"
        return $false
    }

    Write-Host "Falling back to Ninja via VsDevShell (ARM64 native) ..."
    Push-Location $Root
    try {
        . $devShell -Arch arm64 -HostArch arm64 -SkipAutomaticLocation
        $args = @("-S", ".", "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release") + $ExtraDefines
        if ((Invoke-Configure -CMakeArgs $args -Label "Ninja (VsDevShell ARM64)") -ne 0) {
            return $false
        }
    } finally {
        Pop-Location
    }

    $marker = Join-Path $BuildPath "WINTUNE_CMAKE_GENERATOR.txt"
    Set-Content -LiteralPath $marker -Value "Ninja" -Encoding ascii
    Write-Host "Configured with Ninja (ARM64 native)"
    return $true
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

$generators = Get-InstalledVsGenerators -TargetArch $CMakeArch
$attempts = Get-ConfigureAttempts -Generators $generators -NativeArm64:$IsNativeArm64

foreach ($attempt in $attempts) {
    $args = @("-S", ".", "-B", $BuildDir, "-G", $attempt.Name)
    if (-not [string]::IsNullOrWhiteSpace($attempt.Arch)) {
        $args += @("-A", $attempt.Arch)
    }
    if (-not [string]::IsNullOrWhiteSpace($attempt.Instance)) {
        $args += "-DCMAKE_GENERATOR_INSTANCE=$($attempt.Instance)"
    }
    $args += $DefineArg

    if ((Invoke-Configure -CMakeArgs $args -Label $attempt.Label) -eq 0) {
        Write-Host "Configured with $($attempt.Label) (host=$HostArch)"
        exit 0
    }

    if (Test-Path $BuildPath) {
        Remove-Item -LiteralPath $BuildPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
}

if ($IsNativeArm64 -and (Try-NinjaNativeArm64 -ExtraDefines $DefineArg)) {
    exit 0
}

if ($Arch -eq "arm64") {
    Write-Error @"
ARM64 configure failed. No working Visual Studio or Ninja generator was found.

CI: use runs-on: windows-11-vs2026-arm and CMake 4.3+ (Visual Studio 18 2026 generator).
Ensure lukka/get-cmake@latest with cmakeVersion 4.3.3 runs before configure.

Local x64 hosts need the ARM64 MSVC workload, or build on ARM64 Windows.
"@
    exit 1
}

Write-Warning "No Visual Studio generator matched; using CMake default."
$args = @("-S", ".", "-B", $BuildDir) + $DefineArg
if ((Invoke-Configure -CMakeArgs $args -Label "CMake default") -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Configured with CMake default generator ($Arch)"
exit 0
