# WinTune Roadmap

WinTune ships in phases. Each phase builds on a stable core scanner. **v1**
(Phases 0–9) is CLI-first, native, safe-by-default, and SSH-friendly. Later
phases add measured boot diagnostics, a background service, deeper metrics,
update/reboot helpers, fleet automation, and optional local UI — without
Electron, cloud telemetry, or “PC cleaner” behavior.

Initial version: **0.1.0** (semantic versioning).

For the full feature catalog see [`DESIGN.md`](DESIGN.md).

---

## Status Overview

| Phase | Scope | Status |
| ----- | ----- | ------ |
| 0 | Skeleton | Done |
| 1 | Basic metrics | Done |
| 2 | JSON and automation | Done |
| 3 | Watch mode | Done |
| 4 | Recommendations | Done |
| 5 | Startup and services | Done |
| 6 | TUI dashboard | Done |
| 7 | Safe apply actions | Done |
| 8 | Reports | Done |
| 9 | SSH hardening | Done |
| 10 | ETW boot/login analysis | Done |
| 11 | Background agent / Windows Service | Done |
| 12 | Scheduled tasks + startup depth | Done |
| 13 | Windows Update & reboot readiness (`updates`, WT-UPDATE-*) | Done |
| 14 | Restart Manager integration (`blockers`, WT-BLOCKER-001) | Done |
| 15 | Metrics depth (per-process CPU, disk rates, multi-sample scan) | Done |
| 16 | Apply actions v2 + rollback completeness | Done |
| 17 | Fleet / automation hardening | Done |
| 18 | Packaging + ARM64 (developer) | Done |
| 19 | Optional tray / native GUI (never Electron) | Done |
| 20 | TUI polish & UX | Done |
| 21 | Distributable executable (end-user release) | Planned |

---

## v1 (Phases 0–9) — Complete

v1 delivers a native terminal-first performance doctor: measure, explain,
recommend, apply safe fixes, report, and work over SSH.

### Phase 0 — Skeleton

- CMake project and `wintune.exe`.
- `wintune help`, `wintune version`.
- Logging and error handling.
- Basic module structure.

```bash
wintune help
wintune version
```

### Phase 1 — Basic Metrics

- CPU total usage.
- Memory usage.
- Disk free space.
- Top processes by memory.
- Basic text output.

```bash
wintune scan
wintune top
```

### Phase 2 — JSON and Automation

- Small, safe JSON emitter.
- `--json` for scan/top.
- Stable report model.

```bash
wintune scan --json
wintune top --json
```

### Phase 3 — Watch Mode

- Refreshing process table without the full TUI.
- Configurable interval.

```bash
wintune top --watch
```

### Phase 4 — Recommendations

- Recommendation engine.
- Power plan, memory pressure, disk pressure, and startup review
  recommendations.

```bash
wintune recommend
wintune doctor
```

### Phase 5 — Startup and Services

- Registry startup scanner.
- Startup folder scanner.
- Auto-start service scanner.
- Service status listing.

```bash
wintune startup
wintune services
```

### Phase 6 — Terminal Dashboard / TUI

- `wintune tui` with an ANSI/VT renderer.
- Unicode mode, ASCII fallback, no-color, safe-terminal.
- Live CPU/RAM/disk/network/process view.

```bash
wintune tui
wintune tui --safe-terminal
wintune tui --no-unicode
```

### Phase 7 — Safe Apply Actions

- Switch power plan.
- Restart a selected service (with denylist).
- Disable/enable startup entries (StartupApproved flag).
- Rollback metadata.

```bash
wintune apply <id>
wintune power --set performance
wintune startup disable "<id>"
wintune services restart <name>
wintune rollback list
wintune rollback apply <id>
```

### Phase 8 — Reports

- Text performance report (extended beyond scan summary).
- JSON report (same schema as scan).
- Output to file.

```bash
wintune report
wintune report --format json
wintune report --output report.txt
```

### Phase 9 — SSH Hardening

- Session detection (interactive, remote, elevated in JSON).
- Conservative rendering on piped/remote sessions.
- JSON stdout stays clean; hints suppressed in JSON mode.
- Clear admin-required messages over SSH.
- `--yes` for mutating commands over one-shot SSH.

```bash
ssh user@host "wintune scan"
ssh user@host "wintune scan --json"
ssh user@host "wintune top --watch"
ssh -t user@host "wintune tui --safe-terminal"
```

See [`ssh.md`](ssh.md).

---

## v1 Definition of Done

WinTune v1 is complete when it can:

1. Build as a native Windows `.exe`.
2. Run from PowerShell or CMD.
3. Run over SSH as a normal terminal command.
4. Show CPU, memory, disk, and process metrics.
5. Show startup entries.
6. Show a service summary.
7. Show the current power plan.
8. Generate clear recommendations.
9. Export a report as text and JSON.
10. Provide `top --watch`.
11. Provide a basic `tui` dashboard.
12. Apply at least one safe action: switching the power plan.
13. Never perform dangerous changes automatically.
14. Never require Electron or any browser runtime.

**All v1 criteria are met (Phases 0–9).**

---

## v2+ (Phases 10–21)

Phases 10–20 are **Done**. Phase 21 remains for end-user installer polish.

These phases extend WinTune from “on-demand doctor” to “measured boot analysis,
background monitoring, and richer automation” while keeping the same safety
model.

### Phase 10 — ETW Boot / Login Analysis

**Goal:** Replace startup heuristics with measured boot and login attribution.

**Deliver:**

- Boot trace collection (ETW / `.etl` or in-memory session).
- Login trace collection.
- Slow startup attribution (services, apps, drivers).
- Disk-heavy startup detection.
- Summarized output for `scan`, `doctor`, `report`, and `startup` — not raw
  event dumps.

**APIs:** `evntrace.h`, `tdh.h` (`advapi32.lib`, `tdh.lib`).

**Example commands (target):**

```bash
wintune boot trace --duration 60
wintune boot analyze
wintune startup --measured
```

**Depends on:** Stable core scanner (v1). Admin may be required for some
trace sessions.

**Prerequisite:** Phase 10 should not start until PDH/process/service scanning
is stable in the field.

---

### Phase 11 — Background Agent / Windows Service

**Goal:** Optional background monitoring and privileged local actions without
UAC prompts over SSH.

**Deliver:**

- WinTune Windows Service (clearly installed, clearly removable).
- Periodic health scans.
- Local named-pipe control channel (CLI talks to service).
- Privileged apply actions via service (power, services, HKLM startup).
- Scheduled recommendations.

**Safety rules:**

- Never hidden persistence.
- Documented install/uninstall.
- Service identity and permissions explicit in docs.

**Example commands (target):**

```bash
wintune service install
wintune service status
wintune scan --via-service
```

---

### Phase 12 — Scheduled Tasks + Startup Depth ✅ Done

**Goal:** Complete startup impact picture beyond registry and folders.

**Deliver:**

- Scheduled task inspection (`startup --include-tasks` implemented).
- Logon-triggered and startup-heavy task detection.
- Safe disable/delay for user-approved third-party tasks (with rollback).
- Startup delay support (not just enable/disable).
- Measured startup impact when Phase 10 ETW data is available.

**APIs:** Task Scheduler COM APIs.

**Example commands (target):**

```bash
wintune startup --include-tasks
wintune tasks list --logon
wintune tasks delay "<id>" --seconds 30
```

**Never:** Disable Microsoft security or system tasks automatically.

---

### Phase 13 — Windows Update & Reboot Readiness ✅ Done

**Goal:** Explain update and reboot state without becoming an installer.

**Deliver:**

- Report available / pending update state.
- Reboot-required detection.
- Last check / install metadata where available.
- Recommendations (e.g. schedule reboot, free disk space for updates).

**APIs:** Windows Update Agent (`wuapi.h`).

**Example commands (target):**

```bash
wintune updates
wintune updates --json
wintune recommend   # includes WT-UPDATE-* ids
```

**Never:** Auto-install updates or disable Windows Update.

---

### Phase 14 — Restart Manager Integration ✅ Done

**Goal:** Help users understand what blocks updates and restarts.

**Deliver:**

- Detect applications blocking restart/update.
- Detect files locked by processes.
- Suggest graceful close with explicit user confirmation.

**APIs:** Restart Manager (`restartmanager.h`, `rstrtmgr.lib`).

**Example commands (target):**

```bash
wintune blockers
wintune blockers --json
```

**Never:** Force-close applications without explicit confirmation.

---

### Phase 15 — Metrics Depth ✅ Done

**Goal:** Stronger bottleneck detection with richer per-process data.

**Deliver:**

- Per-process CPU via PDH (replace `cpu_percent: null` in JSON).
- Disk and network rates per process where practical.
- Multi-sample scans (`scan --samples N --interval MS`) with smoothed
  recommendations.
- Improved `top --sort cpu|disk` and TUI process columns.

**Example commands (target):**

```bash
wintune scan --samples 5 --interval 1000
wintune top --sort cpu
```

---

### Phase 16 — Apply Actions v2 + Rollback Completeness

**Status:** Done

**Goal:** One coherent apply/rollback story for all mutating features.

**Deliver:**

- Full `rollback list` / `rollback apply` for every mutating action.
- Central recommendation → action map (single source of truth).
- More safe applies tied to recommendations (startup delay, selected tasks).
- Service-based apply when Phase 11 is installed.

**Example commands:**

```bash
wintune rollback list
wintune rollback apply <id>
wintune apply WT-STARTUP-DISABLE "HKCU\Run:App" --yes
wintune apply WT-POWER-001 --via-service --yes
```

See [`docs/apply.md`](apply.md).

---

### Phase 17 — Fleet / Automation Hardening

**Status:** Done

**Goal:** Scriptable remote diagnostics at small scale (homelab, IT teams) —
local-first, no required cloud.

**Deliver:**

- JSON schema versioning and changelog.
- Stable exit codes per failure class.
- Machine-readable error JSON on fatal failures (`--json-errors`).
- Batch-friendly output (`--compact-json`, `--ndjson`).
- Documented SSH/Ansible patterns for many hosts.

**Example usage:**

```bash
for h in host1 host2; do
  ssh "admin@$h" "wintune scan --json --compact-json" > "reports/$h.json"
done
```

See [`json-schema.md`](json-schema.md), [`json-changelog.md`](json-changelog.md),
and [`fleet.md`](fleet.md).

**Never:** Default cloud upload, accounts, or telemetry.

---

### Phase 18 — Packaging + Platform Expansion

**Status:** Done

**Goal:** Make WinTune easy to deploy beyond “build from source.”

**Deliver:**

- `cmake --install` / release packaging (`scripts/package.ps1`).
- ARM64 build and test matrix.
- Optional code signing guidance.
- Optional log file support (`--log-file`) for service/daemon mode.
- Release channels (stable/beta) documented.

**Example:**

```powershell
.\scripts\package.ps1 -Config Release -Arch arm64 -Zip
wintune scan --verbose --log-file C:\ProgramData\WinTune\scan.log
wintune version   # Arch: arm64
```

See [`packaging.md`](packaging.md).

---

### Phase 19 — Optional Local UI (Never Electron) — **Done**

**Goal:** Convenience for non-terminal users without compromising CLI-first
identity.

**Deliver:**

- Tray application (status, “run doctor”, open last report).
- Lightweight native GUI shell (Win32 or equivalent — **not** WebView/Electron).
- Communicates with CLI or Phase 11 service via named pipe.
- Read-only by default; mutating actions still require confirmation.

**Commands:**

```powershell
wintune tray
Start-Tray.cmd
```

See [`tray.md`](tray.md).

**Never:** Electron, Chromium embedded UI, localhost web dashboard as primary
interface.

---

### Phase 20 — TUI Polish & UX — **Done**

**Goal:** Make `wintune tui` feel production-quality — not just functional.

Phase 6 delivered a working dashboard; Phase 20 refines layout, responsiveness,
and readability across Windows Terminal, CMD, SSH, and narrow consoles.

**Deliver:**

- Robust **terminal resize** handling (small/large/SSH PTY).
- **Scrollable or paged** process list; sort toggles (CPU / memory / disk).
- **Per-process CPU and disk-rate columns** when Phase 15 metrics exist.
- Clear **empty and error states** per view (no silent “unavailable”).
- Lower **refresh overhead**; optional pause/freeze frame.
- **Sparkline or mini trend** for CPU/RAM/disk (last N samples, lightweight).
- Optional **`--theme`** or config file for colors/glyphs (still no-color/safe
  fallbacks).
- Optional **snapshot export** (`e` key → write report to `%USERPROFILE%\Documents\WinTune\Reports\`).
- Improved **help overlay** and key hints for SSH/ASCII mode.
- Documented **minimum terminal size** and degraded layout below it.

**Commands:**

```bash
wintune tui
wintune tui --theme compact
wintune tui --safe-terminal --interval 500
wintune tui --sort cpu
```

See [`tui.md`](tui.md).

**Depends on:** Phase 6 (TUI exists). Per-process columns depend on Phase 15.

**Never:** Mouse-required UI, mutating actions from the dashboard without
confirmation, Electron/WebView.

---

### Phase 21 — Distributable Executable (End-User Release)

**Goal:** Users download and run WinTune **without** installing Visual Studio,
CMake, or building from source.

Phase 18 covers **developer** packaging (`cmake --install`, ARM64 builds).
Phase 21 is the **consumer** release: a shippable product artifact.

**Deliver:**

- **Prebuilt `wintune.exe`** on GitHub Releases (x64 first, ARM64 when Phase 18
  is ready).
- **Portable ZIP** — `wintune.exe`, `LICENSE`, `README`, version file.
- Optional **installer** (Inno Setup, WiX MSI, or equivalent — native, no
  Electron bootstrapper).
- **Add to PATH** option during install (or documented manual step).
- Embedded **version resources** — icon, `FileVersion`, `ProductVersion`,
  company/name strings.
- **Application manifest** — `asInvoker` by default, `longPathAware`, compatible
  Windows 10/11.
- **`wintune version`** matches release tag; reproducible CI build.
- **GitHub Actions CI** — `/W4` + `/WX` lint gate, Debug/Release build, smoke
  tests (see [`docs/ci.md`](ci.md)).
- **GitHub Releases** — portable ZIP + SHA-256 checksum on `v*` tags (see
  `scripts/release.ps1`).
- Release **checklist** — checksums (SHA-256), release notes template.
- Optional: **winget** / Chocolatey manifest (community or official).
- Optional: **Authenticode signing** guidance (not required for open source, but
  documented).

**Example user flow (target):**

```text
1. Download WinTune-0.2.0-x64.zip from Releases
2. Extract to C:\Tools\WinTune\
3. Add to PATH (or run full path)
4. wintune doctor
```

**Depends on:** Stable v1+ CLI; Phase 18 helps but Phase 21 can ship a portable
ZIP before MSI/winget.

**Never:** Bundled adware, auto-start without consent, silent background install,
telemetry uploader in the installer.

---

## Recommended Implementation Order (Post-v1)

```text
10  ETW boot/login analysis
11  Windows Service + named-pipe IPC
12  Scheduled tasks + startup depth
13  Windows Update / reboot state
14  Restart Manager
15  Per-process CPU + multi-sample metrics
20  TUI polish (best after 15 for new columns; UX fixes can start earlier)
16  Rollback + apply v2
17  Fleet/automation hardening
18  Packaging + ARM64 (developer)
21  Distributable executable (GitHub Releases, installer, PATH)
19  Tray / optional native GUI
```

Phases 12–16 can be partially reordered, but **10 before 12** (measured
startup) and **11 before 16** (service-based remote apply) are strong
dependencies. **21 after 18** is recommended but a portable ZIP release can
ship before MSI/winget.

---

## Explicit Non-Goals (All Phases)

WinTune will **not** become:

| Category | Examples |
| -------- | -------- |
| PC cleaner / scamware | Registry cleaner, RAM cleaner, “boost FPS” |
| Security bypass | UAC bypass, disable Defender/Update/firewall |
| Heavy runtime | Electron, Node.js, .NET/Python in core |
| Cloud platform | Mandatory telemetry, accounts, upload-by-default |
| Kernel hacks | Undocumented APIs, drivers, injection |
| Debloater | Blind service/startup removal |

Some ideas may become **separate products**; they are not WinTune phases.

---

## What Is ETW? (Phase 10 Primer)

**ETW (Event Tracing for Windows)** is Microsoft’s low-overhead tracing system.
Providers (kernel, services, apps) emit structured events; sessions collect
them for live view or `.etl` analysis.

WinTune uses ETW to answer: *what happened during boot/login, and which
component caused the delay?* Output is **summarized** for users (like Task
Manager or WPT), not raw event log dumps.

---

## Implementation Order (from an empty repo)

Historical bootstrap sequence for v1:

1. `CMakeLists.txt`
2. `src/main.c`
3. `src/common/error.c`
4. `src/common/log.c`
5. `src/cli/cli.c`
6. `src/platform/console.c`
7. `src/metrics/memory.c`
8. `src/metrics/cpu.c`
9. `src/metrics/process.c`
10. `src/output/table.c`
11. `src/output/json.c`
12. `wintune scan`
13. `wintune scan --json`
14. `wintune top`
15. `wintune top --watch`

First useful milestone: `wintune scan`.

---

## Guiding Principle (Every Phase)

Every feature must answer:

```text
What did we measure?
Why does it matter?
What action is safe?
What can go wrong?
How do we undo it?
```

If a feature cannot answer those questions clearly, it does not ship.
