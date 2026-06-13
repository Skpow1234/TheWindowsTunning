# WinTune CLI Reference

WinTune is CLI-first. Every feature is usable from the command line without the
TUI. Output is scriptable and supports a stable JSON mode for automation.

```text
Native Windows performance diagnostics.
Measure bottlenecks. Explain impact. Apply safe fixes.
```

Executable: `wintune.exe` — primary command: `wintune`.

---

## Commands at a Glance

```bash
wintune scan        # full local scan
wintune top         # process usage snapshot (or --watch)
wintune tui         # live terminal dashboard
wintune startup     # startup entries and estimated impact
wintune tasks        # scheduled tasks (logon/boot startup impact)
wintune updates      # Windows Update and reboot readiness
wintune blockers     # apps/files blocking restart or updates
wintune boot        # boot/login performance analysis (Phase 10)
wintune service     # WinTune Windows Service install/manage (Phase 11)
wintune services    # Windows SCM service listing
wintune power       # current power plan + recommendations
wintune recommend   # recommendations without applying
wintune apply       # apply a specific recommendation
wintune report      # write a local performance report
wintune doctor      # scan + recommend + summary (best for normal users)
wintune rollback    # list / apply rollback records
wintune version     # version info
wintune help        # usage
```

---

## Global Options

```bash
--help
--version
--verbose          # show INFO/verbose logs
--debug            # show DEBUG logs
--json             # machine-readable output only
--json-errors      # emit JSON error document on failure
--compact-json     # minified JSON (no pretty-printing)
--ndjson           # one JSON document per line (implies compact)
--no-color         # disable ANSI colors
--no-unicode       # ASCII fallback rendering
--safe-terminal    # conservative rendering for SSH/unknown terminals
--output <path>    # write output to a file
--yes              # confirm mutating actions (dangerous actions still blocked)
--via-service      # route privileged work through WinTune service
```

JSON schema, exit codes, and fleet patterns: [`json-schema.md`](json-schema.md),
[`fleet.md`](fleet.md).

### Global Rules

- Read-only commands must not require admin where possible.
- Mutating commands require confirmation unless `--yes` is explicitly passed.
- Dangerous actions are blocked even with `--yes`.
- JSON mode must not include decorative text.
- TUI mode must not be required for automation.
- CLI must remain fully useful without the TUI.

---

## `wintune scan`

Runs a full local scan: OS version, architecture, uptime, CPU, memory, disk
(usage + active time), network status, top CPU/memory/disk processes, current
power plan, startup entries, auto-start services, and basic recommendations.

```bash
wintune scan
wintune scan --json
wintune scan --output report.json
wintune scan --no-recommendations
wintune scan --samples 5
wintune scan --interval 1000
```

Text output example:

```text
WinTune System Scan

OS: Windows 11 x64
Uptime: 3d 04h
Power: Balanced

CPU:
  Usage: 24.3%
  Logical processors: 16

Memory:
  Used: 21.4 GB / 32.0 GB
  Pressure: Medium

Disk:
  C: 141.2 GB free / 512.0 GB
  Active time: 82%

Top Processes:
PID     Process              CPU%     Memory      Disk I/O
8420    chrome.exe           18.2     2.4 GB      12 MB/s
9921    docker.exe           11.4     1.7 GB      4 MB/s
5312    MsMpEng.exe          8.1      640 MB      51 MB/s

Recommendations:
[WT-POWER-001] Current power plan is Balanced. Use Performance while plugged in.
[WT-DISK-001] Disk active time is high. Review top disk I/O processes.
```

---

## `wintune top`

Displays process usage. Prints a one-time snapshot by default.

```bash
wintune top
wintune top --sort cpu
wintune top --sort memory
wintune top --sort disk
wintune top --limit 20
wintune top --watch
wintune top --interval 1000
wintune top --json
```

`--watch` refreshes the table every interval **without** entering the full TUI.

```text
PID     Process             CPU%     Memory       Disk       Impact
1234    chrome.exe          24.1     2.4 GB       8 MB/s     HIGH
4420    MsMpEng.exe         11.2     640 MB       70 MB/s    DISK
9841    docker.exe          8.9      1.8 GB       2 MB/s     MEDIUM
```

---

## `wintune tui`

Starts the live terminal dashboard. See `tui.md` for layout and controls.

```bash
wintune tui
wintune tui --interval 1000
wintune tui --no-color
wintune tui --no-unicode
wintune tui --safe-terminal
```

---

## `wintune startup`

Shows startup entries and estimated impact.

```bash
wintune startup
wintune startup --json
wintune startup --include-services
wintune startup --include-tasks
wintune startup --measured
wintune startup --interactive
```

Actions (confirmation required unless `--yes`):

```bash
wintune startup disable "<id>"
wintune startup enable "<id>"
wintune startup delay "<id>" --seconds 30
```

Microsoft and security startup items are not disabled by default.

---

## `wintune tasks`

Lists scheduled tasks with startup impact (Phase 12).

```bash
wintune tasks list
wintune tasks list --logon
wintune tasks list --measured
wintune tasks list --json

wintune tasks disable "<id>"
wintune tasks enable "<id>"
wintune tasks delay "<id>" --seconds 30
```

See [`tasks.md`](tasks.md).

---

## `wintune updates`

Reports Windows Update and reboot readiness (Phase 13). Read-only; never installs
updates.

```bash
wintune updates
wintune updates --json
```

See [`updates.md`](updates.md). When reboot is pending, also run `wintune blockers`.

---

## `wintune blockers`

Reports applications and file locks that may block restart or update completion
(Phase 14). Read-only; never closes applications.

```bash
wintune blockers
wintune blockers --json
```

See [`blockers.md`](blockers.md).

---

## `wintune boot`

Analyzes boot and login performance from Windows Diagnostic-Performance events
(ETW-backed, summarized output).

```bash
wintune boot analyze
wintune boot analyze trace.etl
wintune boot trace --duration 60000
wintune boot analyze --json
```

- **`analyze`** — last boot duration, degradation, slow components.
- **`trace`** — live ETW login trace saved to `%LOCALAPPDATA%\WinTune\traces\`.

Admin may be required on some systems. See [`boot.md`](boot.md).

---

## `wintune service`

Install and manage the optional WinTune background agent (Phase 11).

```bash
wintune service status
wintune service install
wintune service start
wintune service stop
wintune service uninstall
```

Use **`--via-service`** on `scan --json`, `doctor --json`, or `apply` to delegate
privileged work to the local agent. See [`service.md`](service.md).

Global option:

```bash
wintune scan --json --via-service
```

---

## `wintune services`

Shows service status and startup type.

```bash
wintune services
wintune services --auto
wintune services --running
wintune services --stopped
wintune services --failed
wintune services --suspicious-impact
wintune services --live
wintune services --json
```

Action (later phases, confirmation required):

```bash
wintune services restart <name>
```

Services are never disabled automatically in v1.

---

## `wintune power`

Shows current power plan and recommendations. Only switches to **existing**
Windows power schemes; a missing scheme produces a clear error.

```bash
wintune power
wintune power --set balanced
wintune power --set performance
wintune power --set saver
wintune power --json
```

```text
Current power plan: Balanced
Power source: AC

Recommendation:
Use High Performance while plugged in if you are compiling, gaming, or running heavy workloads.

Safe actions:
[WT-POWER-001] Switch to High Performance
```

---

## `wintune recommend`

Generates recommendations without applying anything. Categories: performance,
startup, memory, disk, power, services, updates, safety.

```bash
wintune recommend
```

Each recommendation includes: id, title, severity, confidence, reason, expected
impact, risk level, requires-admin, rollback-available, and the command to
apply (if any). Recommendations are deterministic and explainable.

---

## `wintune apply`

Applies a specific recommendation by ID through the central action map
(`docs/apply.md`).

```bash
wintune apply WT-POWER-001
wintune apply WT-POWER-001 --yes
wintune apply WT-STARTUP-DISABLE "HKCU\Run:App"
wintune apply WT-STARTUP-DELAY "HKCU\Run:App" --seconds 30
wintune apply WT-TASK-DELAY "task:\Vendor\App" --seconds 30
wintune apply WT-POWER-001 --via-service --yes
```

- Power recommendations need only the id; startup/task applies require a
  **target id** as the second argument (see `wintune startup`).
- Never applies all recommendations blindly.
- Requires confirmation and admin where necessary (`--yes` for automation/SSH).
- Logs the change and creates rollback data where practical.
- Advisory ids (memory, disk, boot analyze, etc.) report that no automatic
  action exists.
- Dangerous actions remain blocked even with `--yes`.

---

## `wintune rollback`

Lists or applies saved rollback records for power, startup, and task changes.

```bash
wintune rollback list
wintune rollback list --json
wintune rollback apply <id>
wintune rollback apply <id> --yes
```

JSON list entries include `action_id`, `previous_value`, and `new_value`.
See [`docs/apply.md`](apply.md) for record locations and supported types.

Writes a local performance report (system summary, performance summary, top
bottlenecks, startup impact, power plan status, service observations,
recommendations, risk notes).

```bash
wintune report
wintune report --format text
wintune report --format json
wintune report --output wintune-report.txt
```

---

## `wintune doctor`

Friendly all-in-one command, equivalent to `scan + recommend + report summary`.
It never applies changes and is the best command for normal users.

```bash
wintune doctor
```

---

See [`docs/apply.md`](apply.md) for record locations and supported types.

---

## `wintune report`

```text
WinTune 0.1.0
Build: Debug/Release
Compiler: MSVC
Arch: x64
```

---

## Exit Behavior Notes

- In JSON mode: no banners, no progress text, no ANSI colors; partial failure
  data is included inside the JSON where possible.
- Non-interactive sessions are detected and rendered conservatively.
