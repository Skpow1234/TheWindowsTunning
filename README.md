# WinTune

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: Windows 10 | 11](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?logo=windows&logoColor=white)](#supported-platforms)
[![Language: C17](https://img.shields.io/badge/Language-C17-00599C?logo=c&logoColor=white)](#build)
[![Build: CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake&logoColor=white)](#build)
[![Compiler: MSVC](https://img.shields.io/badge/Compiler-MSVC-5C2D91?logo=visualstudio&logoColor=white)](#build)
[![Version: 0.1.0](https://img.shields.io/badge/Version-0.1.0-blue.svg)](docs/roadmap.md)
[![Status: WIP](https://img.shields.io/badge/Status-WIP%20(Phase%209)-orange.svg)](#project-status)

**Native Windows performance diagnostics. Measure bottlenecks. Explain impact. Apply safe fixes.**

WinTune is a native, terminal-first Windows performance doctor written in C. It
inspects your machine using official Windows APIs, explains what it finds in
plain language, and applies only safe, reversible changes after you approve
them.

```text
WinTune System Scan

OS: Windows 11 Pro x64
Host: DESKTOP-9KD2
Uptime: 3d 04h
Power: Balanced (AC)

CPU:
  Usage: 7.7%
  Logical processors: 32

Memory:
  Used: 21.6 GB / 63.9 GB (33.8%)

Disk:
  C:\ 272.8 GB free / 892.8 GB (30.6% free)
  Active time: 4%

Top Processes by Memory:
PID      Process                            Memory      Private
13136    Cursor.exe                         1.1 GB       1.1 GB
24628    Discord.exe                      606.7 MB     533.5 MB

Recommendations:
[WT-POWER-001] Use a higher-performance power plan while plugged in
  Severity: low | Risk: low | Confidence: 85% | reversible
  Why: On AC power with the 'Balanced' plan. While plugged in, the High
       performance plan can improve responsiveness for heavy workloads.
  Action: wintune power --set performance
```

---

## What WinTune is

- **Native and lightweight** — a single `wintune.exe`, no runtime to install.
- **CLI-first, TUI-second** — fully scriptable; an optional live dashboard.
- **SSH-friendly** — runs as a normal terminal command over OpenSSH.
- **Safe by default** — read-only unless you explicitly approve a change.
- **Local-first** — no cloud, no telemetry, no account, no background upload.
- **Explainable** — every recommendation says what was measured, why it
  matters, the action, the risk, and how to undo it.

## What WinTune is **not**

WinTune is **not** a fake "PC cleaner", registry-tweaking booster, debloater,
antivirus replacement, FPS optimizer, or RAM cleaner. It never disables
security tools, deletes system files, or promises magical speedups. See
[`docs/safety.md`](docs/safety.md).

---

## Supported platforms

- Windows 10 and Windows 11, 64-bit (x86_64 / AMD64). ARM64 is planned.
- Builds with MSVC (Visual Studio 2022 Build Tools). clang-cl is optional.

---

## Build

Requirements: CMake 3.24+ and the MSVC C toolchain (Visual Studio 2022 Build
Tools or newer).

```powershell
# Configure (CMake auto-detects the Visual Studio generator)
cmake -S . -B build

# Build
cmake --build build --config Debug
```

The executable is produced at `build\Debug\wintune.exe` (or `Release` when built
with `--config Release`).

Helper scripts are provided in `scripts/` (`build.ps1`, `test.ps1`,
`clean.ps1`, `package.ps1`).

---

## Usage

```powershell
wintune help            # usage
wintune version         # version info
wintune scan            # full local scan (implemented)
wintune top             # top processes by memory (implemented)
wintune top --limit 20  # show more rows
wintune top --watch     # live refresh in place (q or Ctrl+C to quit)
wintune recommend       # explainable recommendations, no changes (implemented)
wintune doctor          # scan + recommendations + summary (implemented)
wintune startup         # startup entries (registry + folders) (implemented)
wintune services        # service state, start type, PID (implemented)
wintune power           # current plan + recommendation (implemented)
wintune tui             # live terminal dashboard (implemented)
```

### Safe apply actions

Mutating commands always confirm first (or take `--yes`), require admin only
when the change does, and write rollback metadata where practical:

```powershell
wintune power                       # show current plan, source, recommendation
wintune power --set performance     # switch to an existing scheme (balanced|performance|saver|ultimate)
wintune apply WT-POWER-001          # apply a recommendation by id
wintune startup disable "<id>"      # toggle a startup entry (StartupApproved flag; reversible)
wintune startup enable  "<id>"
wintune services restart <name>     # stop+start a service (admin; critical services refused)
wintune rollback list               # list saved rollback records
wintune rollback apply <id>         # undo a recorded change
wintune report                      # write a performance report (text or JSON)
```

### Performance report

```powershell
wintune report                           # text report to stdout
wintune report --format json             # JSON report to stdout
wintune report --json                    # same as --format json
wintune report --output report.txt       # write text report to a file
wintune report --format json --output report.json
```

The text report adds bottlenecks, risk notes, and pointers to `startup` /
`services` beyond the basic scan summary. JSON uses the same schema as
`wintune scan --json`.

Rollback records are stored under `%LOCALAPPDATA%\WinTune\rollback`. Power-plan
switches and startup toggles are fully reversible via `rollback apply`.

### Live dashboard (`tui`)

```powershell
wintune tui                    # live CPU/RAM/disk/network + processes
wintune tui --interval 2000    # refresh every 2 seconds (default 1000 ms)
wintune tui --no-color         # disable ANSI colors
wintune tui --no-unicode       # ASCII bars/borders
wintune tui --safe-terminal    # conservative: ASCII + no color (SSH/unknown)
```

Keys: `q` quit (also `Esc`/Ctrl+C), `r` refresh, `o` overview, `d` disk,
`m` memory, `n` network, `p` power, `s` services, `?` help. The dashboard is
read-only and always restores the terminal (cursor + main screen) on exit. It
requires an interactive terminal; in a non-interactive session it exits with a
hint to use `scan` or `top --watch`.

`startup` and `services` are read-only inventories:

```powershell
wintune startup --include-services   # also list auto-start services
wintune startup --json
wintune services --auto              # filters: --auto --running --stopped --failed
wintune services --failed            # auto-start services that are stopped
wintune services --json
```

Common options for `scan`/`top` today:

```powershell
wintune scan --interval 1000   # CPU sampling interval (ms)
wintune top --limit 15         # number of rows
wintune top --watch            # live monitor (default 1000 ms)
wintune top --watch --interval 2000   # refresh every 2 seconds
```

`top --watch` refreshes the table in place without entering the full TUI. It
requires an interactive terminal; in a non-interactive/piped session it prints
a single snapshot instead. The cursor is always restored on exit.

Full command reference: [`docs/cli.md`](docs/cli.md).

### JSON automation

`scan`, `top`, `recommend`, `doctor`, `power`, and `rollback list` support
`--json`, emitting only JSON on stdout so output can be piped and parsed safely.
Use `--output <path>` to write to a file instead (UTF-8, no BOM):

```powershell
wintune scan --json
wintune scan --json --output report.json
wintune top  --json --limit 20
wintune recommend --json
```

The scan document has a stable shape:

```json
{
  "version": "0.1.0",
  "timestamp_utc": "2026-06-11T08:00:13Z",
  "system":  { "available": true, "os": "Windows 11 Pro", "arch": "x64", "hostname": "...", "uptime_ms": 0 },
  "cpu":     { "available": true, "logical_processors": 32, "total_usage_percent": 14.1 },
  "memory":  { "available": true, "total_bytes": 0, "available_bytes": 0, "used_bytes": 0, "used_percent": 35.3 },
  "disk":    { "available": true, "volumes": [ { "root": "C:\\", "total_bytes": 0, "free_bytes": 0, "free_percent": 30.5 } ], "active_available": true, "active_percent": 4.0 },
  "power":   { "available": true, "plan": "Balanced", "plan_name": "Balanced", "on_ac": true, "battery_percent": null },
  "processes": { "available": true, "top": [ { "pid": 0, "name": "...", "working_set_bytes": 0, "private_bytes": 0, "read_bytes": 0, "write_bytes": 0, "cpu_percent": null } ] },
  "recommendations": [
    { "id": "WT-POWER-001", "title": "...", "severity": "low", "risk": "low", "reason": "...", "action": "wintune power --set performance", "requires_admin": false, "rollback_available": true, "confidence_percent": 85 }
  ]
}
```

Each section carries an `available` flag so partial failures are visible
rather than fatal. `cpu_percent` is `null` until per-process CPU lands in a
later phase. Recommendations are deterministic and explainable; pass
`--no-recommendations` to `scan` to omit them.

### Over SSH

WinTune works through the Windows OpenSSH Server as a normal terminal program.
One-shot commands (`ssh host "wintune scan --json"`) are non-interactive: no
ANSI on stdout, no confirmation prompts (use `--yes` for mutating commands).
JSON output includes a `session` object (`interactive`, `remote`, `elevated`).

```bash
ssh user@windows-host "wintune scan"
ssh user@windows-host "wintune scan --json"
ssh user@windows-host "wintune doctor --json"
ssh user@windows-host "wintune top --watch"    # single snapshot over one-shot SSH
ssh -t user@windows-host "wintune tui --safe-terminal"
```

See [`docs/ssh.md`](docs/ssh.md).

---

## Safety principles

- Read-only commands never modify the system and avoid requiring admin.
- Mutating commands require confirmation unless `--yes` is passed.
- Dangerous actions are blocked even with `--yes`.
- Changes write rollback metadata where practical.

Full details: [`docs/safety.md`](docs/safety.md).

---

## Project status

WinTune is under active, phased development.

| Phase | Scope | Status |
| ----- | ----- | ------ |
| 0 | Skeleton: build, `help`, `version`, logging, errors | Done |
| 1 | Basic metrics: `scan`, `top` (CPU, memory, disk, processes) | Done |
| 2 | JSON output for `scan` / `top` (`--json`, `--output`) | Done |
| 3 | `top --watch` live refresh | Done |
| 4 | Recommendation engine, `recommend`, `doctor` | Done |
| 5 | `startup`, `services` | Done |
| 6 | `tui` dashboard | Done |
| 7 | Safe apply actions: `power --set`, `apply`, startup toggle, service restart, `rollback` | Done |
| 8 | `report` (text + JSON, `--output`) | Done |
| 9 | SSH hardening (session detection, safe-terminal, JSON session metadata) | Done |

Commands that are recognized but not yet implemented print a clear notice and
exit non-zero. The full plan is in [`docs/roadmap.md`](docs/roadmap.md).

### Current limitations

- `--json` and `--output` work for `scan`, `top`, `recommend`, `doctor`, `power`,
  `rollback list`, and `report`.
- `top --watch` is live when stdin and stdout are both interactive; it falls
  back to a single snapshot over piped/one-shot SSH (silent fallback with
  `--json`).
- Remote SSH sessions auto-enable conservative rendering (`--safe-terminal`
  semantics) for interactive PTY sessions.
- Process enumeration uses a top-K collector (no full-process buffer) for
  `scan`, `top`, and `tui`.
- Per-process CPU and disk-rate columns are not yet computed (later phases);
  `cpu_percent` is reported as `null` in JSON for now.
- Recommendations cover power, memory, disk free space, disk activity, and CPU.
- Disk active time is a single PDH sample per scan; multi-sample smoothing is
  planned.
- `power --set` switches only to power schemes that already exist on the
  machine (it never creates custom plans); the previous scheme is captured for
  rollback, including custom plans.
- `apply` only auto-applies actionable recommendations (currently the power
  ones). Memory/disk/CPU recommendations are advisory and report that no
  automatic action is taken.
- `startup enable/disable` toggles the Windows StartupApproved flag (the same
  one Task Manager uses) and never deletes the underlying Run value or startup
  file. HKLM-scoped entries (`HKLM\Run`, common Startup folder) require admin.
  Startup "impact" is a coarse heuristic until boot tracing (Phase 10) provides
  measured costs. Scheduled-task inspection (`--include-tasks`) is not yet
  implemented.
- `services restart` requires elevation and refuses a denylist of
  critical/security services to avoid destabilizing Windows.
- The `tui` dashboard is read-only and keyboard-driven (no mouse). Network
  throughput is computed from interface byte-counter deltas between refreshes.
  Applying changes from the dashboard is intentionally not supported.

---

## Documentation

- [`DESIGN.md`](docs/DESIGN.md) — complete feature catalog
- [`docs/architecture.md`](docs/architecture.md) — how the codebase is layered
- [`docs/safety.md`](docs/safety.md) — what WinTune will and will not do
- [`docs/cli.md`](docs/cli.md) — command reference
- [`docs/tui.md`](docs/tui.md) — terminal dashboard
- [`docs/ssh.md`](docs/ssh.md) — remote usage
- [`docs/metrics.md`](docs/metrics.md) — what is measured and how
- [`docs/roadmap.md`](docs/roadmap.md) — phased delivery plan

---

## License

Released under the MIT License. See [`LICENSE`](LICENSE).
