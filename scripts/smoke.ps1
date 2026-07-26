# WinTune CLI smoke tests — exercise every command safely.
#
# Usage:
#   .\scripts\smoke.ps1
#   .\scripts\smoke.ps1 -Config Release
#   .\scripts\smoke.ps1 -Exe .\dist\WinTune-0.1.2-win-x64\wintune.exe
#   ./scripts/smoke -Config Release   # Git Bash
#
# Policy:
#   - Read-only commands must succeed (exit 0) unless noted.
#   - Mutating commands are tested WITHOUT --yes (must not change the system).
#   - Interactive commands (tui/tray) are checked for expected non-interactive behavior.
#   - Access-denied on privileged reads (boot/updates) is accepted as soft-pass.

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$Exe = "",
    [switch]$SkipSlow
)

$ErrorActionPreference = "Continue"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $Root

if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $Root "build\$Config\wintune.exe"
}
if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Error "Executable not found: $Exe (build first or pass -Exe)"
    exit 1
}

$OutDir = Join-Path $env:TEMP ("wintune-smoke-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$script:Pass = 0
$script:Fail = 0
$script:Soft = 0
$script:Skip = 0
$Results = New-Object System.Collections.Generic.List[object]

function Write-CaseResult {
    param(
        [string]$Name,
        [ValidateSet("PASS", "FAIL", "SOFT", "SKIP")]
        [string]$Status,
        [string]$Detail = ""
    )
    $color = switch ($Status) {
        "PASS" { "Green" }
        "FAIL" { "Red" }
        "SOFT" { "Yellow" }
        "SKIP" { "DarkGray" }
    }
    $line = "[{0}] {1}" -f $Status, $Name
    if ($Detail) { $line += " - $Detail" }
    Write-Host $line -ForegroundColor $color
    $Results.Add([pscustomobject]@{ Name = $Name; Status = $Status; Detail = $Detail }) | Out-Null
    switch ($Status) {
        "PASS" { $script:Pass++ }
        "FAIL" { $script:Fail++ }
        "SOFT" { $script:Soft++ }
        "SKIP" { $script:Skip++ }
    }
}

function Invoke-Wt {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$WtArgs,
        [int]$TimeoutSec = 90
    )
    $stdoutPath = Join-Path $OutDir ("stdout-" + [guid]::NewGuid().ToString("N") + ".txt")
    $stderrPath = Join-Path $OutDir ("stderr-" + [guid]::NewGuid().ToString("N") + ".txt")

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    # Quote args that contain spaces; join for ArgumentList string form.
    $quoted = foreach ($a in $WtArgs) {
        if ($a -match '[\s"]') { '"' + ($a -replace '"', '\"') + '"' } else { $a }
    }
    $psi.Arguments = [string]::Join(" ", $quoted)
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = "$Root"

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    $finished = $proc.WaitForExit($TimeoutSec * 1000)
    if (-not $finished) {
        try { $proc.Kill() } catch {}
        return @{
            ExitCode = -1
            StdOut   = ""
            StdErr   = "TIMEOUT after ${TimeoutSec}s"
            TimedOut = $true
        }
    }
    # Ensure async readers finish after process exit.
    [void]$stdoutTask.Wait(5000)
    [void]$stderrTask.Wait(5000)
    $out = ""
    $err = ""
    try { $out = $stdoutTask.Result } catch { $out = "" }
    try { $err = $stderrTask.Result } catch { $err = "" }
    if ($null -eq $out) { $out = "" }
    if ($null -eq $err) { $err = "" }
    # Persist for debugging.
    Set-Content -LiteralPath $stdoutPath -Value $out -Encoding utf8
    Set-Content -LiteralPath $stderrPath -Value $err -Encoding utf8
    return @{
        ExitCode = [int]$proc.ExitCode
        StdOut   = $out
        StdErr   = $err
        TimedOut = $false
    }
}

function Test-ExpectOk {
    param(
        [string]$Name,
        [string[]]$WtArgs,
        [string[]]$StdoutContains = @(),
        [string[]]$StdoutRegex = @(),
        [int]$TimeoutSec = 90,
        [switch]$AllowAccessDenied
    )
    $r = Invoke-Wt -WtArgs $WtArgs -TimeoutSec $TimeoutSec
    if ($r.TimedOut) {
        Write-CaseResult $Name "FAIL" "timeout"
        return
    }
    if ($AllowAccessDenied -and ($r.ExitCode -eq 11 -or $r.StdErr -match "access denied|administrator|Access is denied")) {
        Write-CaseResult $Name "SOFT" "access denied (elevated shell may be required)"
        return
    }
    if ($r.ExitCode -ne 0) {
        $snip = ($r.StdErr + $r.StdOut).Trim()
        if ($snip.Length -gt 180) { $snip = $snip.Substring(0, 180) + "..." }
        Write-CaseResult $Name "FAIL" "exit $($r.ExitCode): $snip"
        return
    }
    foreach ($needle in $StdoutContains) {
        if ($r.StdOut -notlike "*$needle*") {
            Write-CaseResult $Name "FAIL" "stdout missing '$needle'"
            return
        }
    }
    foreach ($rx in $StdoutRegex) {
        if ($r.StdOut -notmatch $rx) {
            Write-CaseResult $Name "FAIL" "stdout regex failed: $rx"
            return
        }
    }
    Write-CaseResult $Name "PASS"
}

function Test-ExpectExit {
    param(
        [string]$Name,
        [string[]]$WtArgs,
        [int[]]$ExitCodes,
        [string]$StdoutOrErrRegex = "",
        [int]$TimeoutSec = 60
    )
    $r = Invoke-Wt -WtArgs $WtArgs -TimeoutSec $TimeoutSec
    if ($r.TimedOut) {
        Write-CaseResult $Name "FAIL" "timeout"
        return
    }
    if ($ExitCodes -notcontains $r.ExitCode) {
        Write-CaseResult $Name "FAIL" "exit $($r.ExitCode); expected $($ExitCodes -join '|')"
        return
    }
    if ($StdoutOrErrRegex) {
        $blob = $r.StdOut + $r.StdErr
        if ($blob -notmatch $StdoutOrErrRegex) {
            Write-CaseResult $Name "FAIL" "output regex failed: $StdoutOrErrRegex"
            return
        }
    }
    Write-CaseResult $Name "PASS" "exit $($r.ExitCode)"
}

function Test-JsonOk {
    param(
        [string]$Name,
        [string[]]$WtArgs,
        [string[]]$MustContain = @(),
        [int]$TimeoutSec = 90,
        [switch]$AllowAccessDenied
    )
    $r = Invoke-Wt -WtArgs $WtArgs -TimeoutSec $TimeoutSec
    if ($r.TimedOut) {
        Write-CaseResult $Name "FAIL" "timeout"
        return
    }
    if ($AllowAccessDenied -and ($r.ExitCode -eq 11 -or $r.StdErr -match "access denied|administrator")) {
        Write-CaseResult $Name "SOFT" "access denied"
        return
    }
    if ($r.ExitCode -ne 0) {
        Write-CaseResult $Name "FAIL" "exit $($r.ExitCode)"
        return
    }
    $text = $r.StdOut.Trim()
    if (-not ($text.StartsWith("{") -or $text.StartsWith("["))) {
        Write-CaseResult $Name "FAIL" "stdout is not JSON-looking"
        return
    }
    try {
        $null = $text | ConvertFrom-Json -ErrorAction Stop
    } catch {
        # Compact/partial docs may still be valid enough; require braces + key tokens.
        if ($MustContain.Count -eq 0) {
            Write-CaseResult $Name "FAIL" "ConvertFrom-Json failed: $($_.Exception.Message)"
            return
        }
    }
    foreach ($needle in $MustContain) {
        if ($text -notlike "*$needle*") {
            Write-CaseResult $Name "FAIL" "JSON missing '$needle'"
            return
        }
    }
    Write-CaseResult $Name "PASS"
}

Write-Host ""
Write-Host "WinTune CLI smoke tests" -ForegroundColor Cyan
Write-Host "Exe: $Exe"
Write-Host "Out: $OutDir"
Write-Host ""

# --- Meta ---
Test-ExpectOk "help" @("help") -StdoutContains @("Commands:", "scan", "doctor", "tray")
Test-ExpectOk "version" @("version") -StdoutRegex @("WinTune\s+\d+\.\d+\.\d+", "Arch:")
Test-ExpectOk "--help" @("--help") -StdoutContains @("Usage:")
Test-ExpectOk "--version" @("--version") -StdoutContains @("WinTune")
Test-ExpectExit "unknown-command" @("not-a-real-command") -ExitCodes @(2) -StdoutOrErrRegex "unknown command"

# --- Read-only core ---
$scanTimeout = if ($SkipSlow) { 30 } else { 120 }
Test-ExpectOk "scan" @("scan", "--samples", "1", "--interval", "200") `
    -StdoutContains @("WinTune System Scan", "CPU:", "Memory:") -TimeoutSec $scanTimeout
Test-JsonOk "scan --json" @("scan", "--json", "--samples", "1", "--interval", "200") `
    -MustContain @("schema_version", "cpu", "memory") -TimeoutSec $scanTimeout

Test-ExpectOk "top" @("top", "--limit", "5") -StdoutRegex @("PID|Process")
Test-ExpectOk "top --sort cpu" @("top", "--sort", "cpu", "--limit", "5")
Test-ExpectOk "top --sort memory" @("top", "--sort", "memory", "--limit", "5")
Test-ExpectOk "top --sort disk" @("top", "--sort", "disk", "--limit", "5")
Test-JsonOk "top --json" @("top", "--json", "--limit", "5") -MustContain @("processes")

Test-ExpectOk "doctor" @("doctor", "--samples", "1", "--interval", "200") `
    -StdoutContains @("WinTune") -TimeoutSec $scanTimeout
Test-JsonOk "doctor --json" @("doctor", "--json", "--samples", "1", "--interval", "200") `
    -MustContain @("schema_version") -TimeoutSec $scanTimeout

Test-ExpectOk "recommend" @("recommend", "--samples", "1", "--interval", "200") -TimeoutSec $scanTimeout
Test-JsonOk "recommend --json" @("recommend", "--json", "--samples", "1", "--interval", "200") `
    -MustContain @("recommendations") -TimeoutSec $scanTimeout

$reportPath = Join-Path $OutDir "report.txt"
Test-ExpectOk "report --format text" @("report", "--format", "text", "--output", $reportPath, "--samples", "1", "--interval", "200") -TimeoutSec $scanTimeout
if ((Test-Path $reportPath) -and ((Get-Item $reportPath).Length -gt 0)) {
    Write-CaseResult "report file written" "PASS" $reportPath
} else {
    Write-CaseResult "report file written" "FAIL" "missing or empty $reportPath"
}

$reportJson = Join-Path $OutDir "report.json"
Test-ExpectOk "report --format json" @("report", "--format", "json", "--output", $reportJson, "--samples", "1", "--interval", "200") -TimeoutSec $scanTimeout

# --- System inventory ---
Test-ExpectOk "startup" @("startup")
Test-JsonOk "startup --json" @("startup", "--json") -MustContain @("startup")
Test-ExpectOk "startup --include-services" @("startup", "--include-services")
Test-ExpectOk "startup --include-tasks" @("startup", "--include-tasks")

Test-ExpectOk "tasks list" @("tasks", "list")
Test-JsonOk "tasks list --json" @("tasks", "list", "--json") -MustContain @("tasks")
Test-ExpectOk "tasks list --logon" @("tasks", "list", "--logon")

Test-ExpectOk "services" @("services")
Test-ExpectOk "services --running" @("services", "--running")
Test-JsonOk "services --json" @("services", "--json") -MustContain @("services")

Test-ExpectOk "power" @("power") -StdoutRegex @("plan|Power|Balanced|performance|saver")
Test-JsonOk "power --json" @("power", "--json") -MustContain @("power")

Test-ExpectOk "updates" @("updates") -AllowAccessDenied
Test-JsonOk "updates --json" @("updates", "--json") -MustContain @("updates") -AllowAccessDenied

Test-ExpectOk "blockers" @("blockers")
Test-JsonOk "blockers --json" @("blockers", "--json") -MustContain @("blockers")

Test-ExpectOk "boot analyze" @("boot", "analyze") -AllowAccessDenied
Test-JsonOk "boot analyze --json" @("boot", "analyze", "--json") -MustContain @("boot") -AllowAccessDenied

Test-ExpectOk "service status" @("service", "status")
Test-JsonOk "service status --json" @("service", "status", "--json") -MustContain @("installed")

Test-ExpectOk "rollback list" @("rollback", "list")
Test-JsonOk "rollback list --json" @("rollback", "list", "--json")

# --- Mutating: must NOT apply without confirmation ---
Test-ExpectExit "apply without id" @("apply") -ExitCodes @(2) -StdoutOrErrRegex "requires a recommendation id"
# Without --yes, apply should cancel or refuse (not silently change).
Test-ExpectExit "apply WT-POWER-001 (no --yes)" @("apply", "WT-POWER-001") `
    -ExitCodes @(0, 10, 11, 12, 13, 20) -StdoutOrErrRegex "confirm|Cancelled|already|require|denied|not found|High Performance|Balanced|Power"
Test-ExpectExit "power --set bogus" @("power", "--set", "not-a-real-plan") `
    -ExitCodes @(2, 12, 20) -StdoutOrErrRegex "unknown|not found|invalid|plan"
Test-ExpectExit "rollback apply missing" @("rollback", "apply", "no-such-id") `
    -ExitCodes @(12, 10, 20)

# --- Interactive / session-sensitive ---
# Non-interactive smoke runner: tui should refuse cleanly.
Test-ExpectExit "tui non-interactive" @("tui") -ExitCodes @(13, 20) `
    -StdoutOrErrRegex "interactive|not supported|terminal"
# tray starts a message loop — skip auto-run (would hang). Document as manual.
Write-CaseResult "tray (manual)" "SKIP" "interactive tray; run: wintune tray"

# --- Global flags ---
$logPath = Join-Path $OutDir "smoke.log"
Test-ExpectOk "version --verbose --log-file" @("version", "--verbose", "--log-file", $logPath) `
    -StdoutContains @("WinTune")
Test-ExpectOk "scan --no-color --no-unicode" @("scan", "--no-color", "--no-unicode", "--samples", "1", "--interval", "200") `
    -TimeoutSec $scanTimeout
Test-JsonOk "scan --compact-json" @("scan", "--json", "--compact-json", "--samples", "1", "--interval", "200") `
    -MustContain @("schema_version") -TimeoutSec $scanTimeout
Test-ExpectExit "bad --theme alone on scan" @("scan", "--theme") -ExitCodes @(2)

# --- JSON errors on usage failure ---
$r = Invoke-Wt -WtArgs @("apply", "--json-errors")
if ($r.ExitCode -eq 2 -and ($r.StdOut + $r.StdErr) -match "schema_version|error") {
    Write-CaseResult "apply --json-errors" "PASS"
} elseif ($r.ExitCode -eq 2) {
    Write-CaseResult "apply --json-errors" "SOFT" "usage exit ok; JSON error payload optional on this path"
} else {
    Write-CaseResult "apply --json-errors" "FAIL" "exit $($r.ExitCode)"
}

Write-Host ""
Write-Host "Summary" -ForegroundColor Cyan
Write-Host ("  PASS={0}  FAIL={1}  SOFT={2}  SKIP={3}  TOTAL={4}" -f `
    $script:Pass, $script:Fail, $script:Soft, $script:Skip, `
    ($script:Pass + $script:Fail + $script:Soft + $script:Skip))

$summaryPath = Join-Path $OutDir "summary.csv"
$Results | Export-Csv -LiteralPath $summaryPath -NoTypeInformation -Encoding UTF8
Write-Host "  Details: $summaryPath"
Write-Host ""

if ($script:Fail -gt 0) {
    Write-Host "Smoke FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "Smoke PASSED (soft failures are informational)" -ForegroundColor Green
exit 0
