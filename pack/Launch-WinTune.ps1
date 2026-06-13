# WinTune interactive launcher (PowerShell)
# Double-click Launch-WinTune.cmd or run this script from the WinTune folder.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

$exe = Join-Path $PSScriptRoot 'wintune.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "wintune.exe not found in $PSScriptRoot" -ForegroundColor Red
    Read-Host 'Press Enter to close'
    exit 1
}

Write-Host ''
Write-Host "  WinTune interactive mode (PowerShell)" -ForegroundColor Cyan
Write-Host '  Type any wintune command, a number 1-9 for shortcuts, or help.'
Write-Host '  quit | exit | q to leave.'
Write-Host ''
Write-Host '  1 doctor   2 scan   3 top   4 startup   5 power'
Write-Host '  6 recommend   7 help   8 tui   9 rollback list'
Write-Host ''

$shortcuts = @{
    '1' = 'doctor'
    '2' = 'scan'
    '3' = 'top'
    '4' = 'startup'
    '5' = 'power'
    '6' = 'recommend'
    '7' = 'help'
    '8' = 'tui'
    '9' = 'rollback list'
}

while ($true) {
    $line = Read-Host 'wintune'
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $trim = $line.Trim()
    if ($trim -match '^(q|quit|exit)$') { break }
    if ($shortcuts.ContainsKey($trim)) {
        $cmd = $shortcuts[$trim]
        Write-Host "  -> wintune $cmd"
        $trim = $cmd
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = $trim
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit() | Out-Null
}

Write-Host ''
Write-Host 'Goodbye.'
Read-Host 'Press Enter to close'
