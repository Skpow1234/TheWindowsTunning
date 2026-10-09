# Fail if a packaged wintune.exe imports the dynamic VC++ runtime.
# WinGet validation runs the portable command on a clean image. VCRUNTIME140.dll
# is not there, so the process exits STATUS_DLL_NOT_FOUND (-1073741515)
# before main. The shipping binary must be linked /MT.
#
# Usage:
#   .\scripts\verify-portable-exe.ps1 -Path build\Release\wintune.exe

param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [switch]$List
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Error "Executable not found: $Path"
    exit 1
}

function Get-PeImportDlls {
    param([string]$FilePath)

    $bytes = [System.IO.File]::ReadAllBytes($FilePath)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE file: $FilePath"
    }

    function Read-U16([int]$Offset) { return [BitConverter]::ToUInt16($bytes, $Offset) }
    function Read-U32([int]$Offset) { return [BitConverter]::ToUInt32($bytes, $Offset) }

    $lfanew = [int](Read-U32 0x3C)
    if ((Read-U32 $lfanew) -ne 0x00004550) {
        throw "Missing PE signature: $FilePath"
    }

    $coff = $lfanew + 4
    $sectionCount = [int](Read-U16 ($coff + 2))
    $optionalSize = [int](Read-U16 ($coff + 16))
    $optional = $coff + 20
    $magic = Read-U16 $optional
    if ($magic -eq 0x20B) {
        $dataDir = $optional + 112
    } elseif ($magic -eq 0x10B) {
        $dataDir = $optional + 96
    } else {
        throw "Unknown PE optional-header magic 0x$($magic.ToString('X'))"
    }

    $importRva = Read-U32 ($dataDir + 8)
    if ($importRva -eq 0) {
        return @()
    }

    $section = $optional + $optionalSize
    function Convert-RvaToOffset([uint32]$Rva) {
        for ($i = 0; $i -lt $sectionCount; $i++) {
            $header = $section + ($i * 40)
            $virtualAddress = Read-U32 ($header + 12)
            $virtualSize = Read-U32 ($header + 8)
            $rawPointer = Read-U32 ($header + 20)
            $rawSize = Read-U32 ($header + 16)
            $span = [Math]::Max($virtualSize, $rawSize)
            if ($Rva -ge $virtualAddress -and $Rva -lt ($virtualAddress + $span)) {
                return [int]($rawPointer + ($Rva - $virtualAddress))
            }
        }
        throw "Import RVA 0x$($Rva.ToString('X')) is outside every section"
    }

    $names = New-Object System.Collections.Generic.List[string]
    $descriptor = Convert-RvaToOffset $importRva
    while ($true) {
        $nameRva = Read-U32 ($descriptor + 12)
        if ($nameRva -eq 0) {
            break
        }
        $nameOffset = Convert-RvaToOffset $nameRva
        $end = $nameOffset
        while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) {
            $end++
        }
        $names.Add([System.Text.Encoding]::ASCII.GetString($bytes, $nameOffset, $end - $nameOffset))
        $descriptor += 20
    }
    return $names
}

$blocked = @(
    "VCRUNTIME140.dll",
    "VCRUNTIME140_1.dll",
    "MSVCP140.dll",
    "MSVCP140_1.dll",
    "MSVCP140_2.dll",
    "CONCRT140.dll",
    "VCCORLIB140.dll"
)

$imports = @(Get-PeImportDlls -FilePath $Path)
$found = @($imports | Where-Object {
    $name = $_
    $blocked | Where-Object { $_ -eq $name }
})

if ($found.Count -gt 0) {
    Write-Error @"
$Path imports the dynamic VC++ runtime: $($found -join ', ').

WinGet's validation machine does not have Microsoft.VCRedist installed, so
wintune.exe exits -1073741515 (STATUS_DLL_NOT_FOUND). Link the shipping
binary with the static CRT (MSVC_RUNTIME_LIBRARY=MultiThreaded).
"@
    exit 1
}

if ($List) {
    $imports | ForEach-Object { Write-Host $_ }
}

Write-Host "Portable CRT check passed: $Path"
exit 0
