# Add the WinTune folder (containing wintune.exe) to the current user's PATH.
# Usage (from an extracted ZIP or install folder):
#   powershell -ExecutionPolicy Bypass -File .\Add-To-Path.ps1
#   powershell -ExecutionPolicy Bypass -File .\Add-To-Path.ps1 -Remove

param(
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$Dir = (Resolve-Path (Split-Path -Parent $MyInvocation.MyCommand.Path)).Path

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($null -eq $userPath) { $userPath = "" }

$parts = @($userPath -split ";" | Where-Object { $_ -ne "" })
$exists = $parts | Where-Object { $_.TrimEnd('\') -ieq $Dir.TrimEnd('\') }

if ($Remove) {
    if (-not $exists) {
        Write-Host "Not on user PATH: $Dir"
        exit 0
    }
    $newParts = $parts | Where-Object { $_.TrimEnd('\') -ine $Dir.TrimEnd('\') }
    [Environment]::SetEnvironmentVariable("Path", ($newParts -join ";"), "User")
    Write-Host "Removed from user PATH: $Dir"
    Write-Host "Open a new terminal for the change to take effect."
    exit 0
}

if ($exists) {
    Write-Host "Already on user PATH: $Dir"
    exit 0
}

$newPath = if ($userPath.Trim() -eq "") { $Dir } else { "$userPath;$Dir" }
[Environment]::SetEnvironmentVariable("Path", $newPath, "User")
Write-Host "Added to user PATH: $Dir"
Write-Host "Open a new terminal, then run: wintune doctor"
exit 0
