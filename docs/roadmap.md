# WinTune Roadmap

WinTune ships in phases. Each phase builds on a stable core scanner. **v1**
(Phases 0–9) is CLI-first, native, safe-by-default, and SSH-friendly. **v2**
(Phases 10–21) adds measured boot diagnostics, a background service, deeper
metrics, update/reboot helpers, fleet automation, and optional local UI.
**v3** (Phases 22–53) deepens attribution, metrics, boot analysis, apply UX,
and distribution — without Electron, cloud telemetry, or “PC cleaner” behavior.

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
| 21 | Distributable executable (end-user release) | Done |
| 22 | Publisher & signature metadata | Done |
| 23 | Binary provenance | Done |
| 24 | Smart impact scoring v2 | Done |
| 25 | Disk counters in scan model | Done |
| 26 | Per-volume disk activity | Done |
| 27 | Per-process network (ETW) | Done |
| 28 | GPU / display readiness (read-only) | Done |
| 29 | Thermal & power budget (read-only) | Done |
| 30 | Multi-sample disk smoothing | Done |
| 31 | Reboot-spanning boot ETW | Done |
| 32 | Cold vs warm boot profiles | Done |
| 33 | Driver / service start waterfall | Done |
| 34 | Startup delay orchestration | Done |
| 35 | Recommendation confidence engine | Done |
| 36 | Apply preview / dry-run | Done |
| 37 | Service restart rollback metadata | Done |
| 38 | Guided doctor plan | Done |
| 39 | Uninstall advisor (read-only) | Done |
| 40 | TUI historical sparklines | Done |
| 41 | TUI before/after compare | Done |
| 42 | Tray mini-doctor | Done |
| 43 | Accessibility & SSH TUI | Done |
| 44 | Service policy profiles | Done |
| 45 | Named-pipe ACL hardening | Done |
| 46 | Fleet report pack | Done |
| 47 | JSON schema v2 + compatibility | Done |
| 48 | Storage health (read-only) | Done |
| 49 | Memory dump / WER signals | Done |
| 50 | Pagefile & commit charge depth | Planned |
| 51 | Scheduled maintenance windows | Planned |
| 52 | Authenticode CI + release signing | Planned |
| 53 | Installer & channel maturity | Planned |

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

## v2+ (Phases 10–21) — Complete

Phases 10–21 are **Done**. They extended WinTune from “on-demand doctor” to
“measured boot analysis, background monitoring, and richer automation” while
keeping the same safety model.

Winget package submission (`WinTune.WinTune`) and Authenticode-in-CI remain
optional distribution work (see Phases 52–53).

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

**Status:** Done.

**Goal:** Users download and run WinTune **without** installing Visual Studio,
CMake, or building from source.

Phase 18 covers **developer** packaging (`cmake --install`, ARM64 builds).
Phase 21 is the **consumer** release: a shippable product artifact.

**Delivered:**

- Prebuilt `wintune.exe` on GitHub Releases (x64 + ARM64).
- Portable ZIP with `LICENSE`, `README`, `VERSION.txt`, `CHANNEL.txt`, `ARCH.txt`,
  launchers, and `Add-To-Path.*`.
- Embedded **icon**, **VERSIONINFO**, and **application manifest**
  (`resources/`, linked via `wintune.rc`).
- Optional **Inno Setup** script: `scripts/installer/wintune.iss` (PATH task).
- Release checklist: [`release-checklist.md`](release-checklist.md).
- Signing guidance: [`signing.md`](signing.md).
- CI build/lint + GitHub Releases ZIPs + SHA-256 (`scripts/release.ps1`).

**Optional / deferred:** winget/Chocolatey; Authenticode signing in CI.

**Example user flow:**

```text
1. Download WinTune-<ver>-win-x64.zip from Releases
2. Extract to C:\Tools\WinTune\
3. Optional: Add-To-Path.cmd
4. wintune doctor
```

**Never:** Bundled adware, auto-start without consent, silent background install,
telemetry uploader in the installer.

---

## v3 (Phases 22–53) — Planned

These phases deepen WinTune **inside the existing product identity**: native C,
measure → explain → optional confirmed apply → rollback, CLI/TUI/SSH-first.

They mostly package capabilities that specialists can already find in Task
Manager, Sysinternals, WPT, or `powercfg`. The value is a calmer, scriptable,
safe **performance doctor** workflow — not inventing secret Windows features.

**Never (all v3 phases):** cleaners, FPS boosters, debloaters, security bypasses,
Electron/WebView, cloud telemetry, kernel drivers, silent uninstall, UAC bypass.

### Attribution & trust

#### Phase 22 — Publisher & Signature Metadata

**Status:** Done

**Goal:** Show who owns high-impact processes, startups, and services.

**Deliver:**

- Publisher / company strings where available (`CompanyName` version resource).
- Authenticode status via `WinVerifyTrust` (offline-friendly; cache by path).
- Clearer “Microsoft vs third-party” cues in `startup`, `services`, `top`, scan,
  report, and recommendations (Microsoft-origin startups are not suggested for
  disable).
- JSON fields: `publisher`, `signature`, `origin`, `image_path` (plus
  `configured_image` on services).

**Example:**

```bash
wintune startup --json
wintune services --json
wintune top --json
```

**Depends on:** Phases 5, 12, 15.

**Never:** Treat unsigned alone as malware; scareware language.

---

#### Phase 23 — Binary Provenance

**Status:** Done

**Goal:** Map running impact back to on-disk identity.

**Deliver:**

- Resolve process/startup/service path → `ProductName` / `CompanyName` (version
  resources) in `WT_FileIdentity`.
- Classify install location (`windows`, `program-files`, `user-appdata`, `temp`,
  `downloads`, `other`, …).
- Flag `unusual_location` for calm review only (Temp/Downloads, or
  Microsoft-labeled binary outside Windows/Program Files).
- Surface provenance in `scan` / `top` / `startup` / `services` / `report` text
  and JSON (`product_name`, `location`, `unusual_location`).

**Depends on:** Phase 22.

**Never:** Quarantine, delete, or block binaries; scareware language.

---

#### Phase 24 — Smart Impact Scoring v2

**Status:** Done

**Goal:** Replace crude HIGH/MED heuristics with evidence-based scores.

**Deliver:**

- Deterministic `wt_impact_score_compute` (`src/core/impact_score.c`) combining
  measured boot delay, name heuristic, publisher/origin, unusual location, and
  optional matched process CPU/RAM/disk samples.
- Per-item `impact_score` (0–100) + `impact_confidence` on startup/tasks — not a
  fake whole-PC score.
- Recommendation thresholds: score ≥ 65 and confidence ≥ 50
  (`WT-STARTUP-002`, `WT-TASK-001`).
- Documented formula in `docs/metrics.md`.

**Depends on:** Phases 10, 15, 22.

**Never:** Fake “PC score” or fear-based percentages.

---

### Metrics depth

#### Phase 25 — Disk Counters in Scan Model

**Status:** Done

**Goal:** First-class disk throughput in scan/JSON, not only `% Disk Time`.

**Deliver:**

- `wt_collect_disk_io`: one PDH sample for `% Disk Time`, Read/Write Bytes/sec,
  and Avg. Disk Queue Length (`PhysicalDisk(_Total)`).
- Scan report fields: `disk_read_bytes_per_sec`, `disk_write_bytes_per_sec`,
  `disk_avg_queue_length` (+ availability flags); averaged across multi-sample
  scans.
- Text/JSON in `scan` / `doctor` / `report`.
- `WT-DISK-001` cites MB/s when available; `WT-DISK-003` for high throughput
  without extreme active %.

**Depends on:** Phase 15.

---

#### Phase 26 — Per-Volume Disk Activity

**Status:** Done

**Goal:** Separate C: vs D: (etc.) disk pressure.

**Deliver:**

- `wt_collect_disk_io_ex`: one PDH window for `PhysicalDisk(_Total)` plus
  `LogicalDisk(X:)` active %, read/write bytes/sec, and queue per fixed volume.
- Scan merges per-volume activity across multi-sample runs.
- Text/JSON in `scan` / `doctor` / `report` show per-volume activity beside
  free space; totals remain labeled as system-wide.
- `WT-DISK-001` cites the hottest volume when known; `WT-DISK-004` when one
  volume is hot while system total is not.

**Depends on:** Phase 25.

---

#### Phase 27 — Per-Process Network (ETW)

**Status:** Done

**Goal:** Optional top send/receive by process.

**Deliver:**

- Opt-in IP Helper TCP Extended Stats (`GetExtendedTcpTable` +
  `GetPerTcpConnectionEStats`) for per-process send/recv rates (equivalent
  official path to always-on TCPIP ETW; no payload capture).
- Off by default (`--include-network` or `--sort network`; TUI `w` / `4`).
- Clear overhead note in CLI help/docs; UDP not included.
- Net R / Net W columns in `top` text + JSON; Net column in TUI when enabled.

**Depends on:** Phases 10, 15.

**Never:** Packet capture of payloads; credential sniffing.

---

#### Phase 28 — GPU / Display Readiness (Read-Only)

**Status:** Done

**Goal:** High-level GPU/display utilization when official APIs allow.

**Deliver:**

- DXGI hardware adapter enumeration (name, dedicated/shared memory; skip
  software/WARP).
- PDH `\GPU Engine(*)\Utilization Percentage` aggregated by adapter LUID
  (max engine busy, clamped 0–100%).
- Active display count + primary resolution.
- Text/JSON in `scan` / `doctor` / `report`; TUI view (`g`).
- Advisory `WT-GPU-001` when max engine busy ≥ 90% (informational only).

**Never:** Overclocking, undervolt, driver install/update, FPS “boost.”

---

#### Phase 29 — Thermal & Power Budget (Read-Only)

**Status:** Done

**Goal:** Tie “feels slow” to power source and battery drain.

**Deliver:**

- `SYSTEM_BATTERY_STATE` via `CallNtPowerInformation`: present/charging/
  discharging, signed discharge rate (mW), estimated remaining time, capacity.
- Active-plan processor max (PROCTHROTTLEMAX) for AC and DC — calm “capped”
  hint when the current source is below 100%.
- Text/JSON in `power`, `scan`, `doctor`, `report`, TUI power view.
- `WT-POWER-003` (plan caps processor); `WT-POWER-004` (fast discharge ≥ ~20 W).
- Existing `WT-POWER-001` / `002` still link to existing Windows plans only.

**Depends on:** Phase 7 power APIs.

**Never:** Fan curve hacking or firmware flashes. Does not use obsolete
`CurrentMhz` thermal APIs.

---

#### Phase 30 — Multi-Sample Disk Smoothing

**Goal:** Reduce one-spike false alarms for disk active time.

**Status:** Done

**Deliver:**

- Average / sustained disk active % across `scan --samples`.
- Track `disk_active_ok_samples`, `disk_active_hot_samples` (>= 90%), and
  `disk_active_max_percent` in the scan merge.
- Confidence-aware `WT-DISK-001`: fires when average >= 90% **or** a strict
  majority of successful samples were hot (not a single spike).
- Reason text cites N of M samples and peak; JSON exposes the new fields;
  text/report show avg / peak / hot counts.

**Depends on:** Phases 4, 15.

---

### Boot & startup v2

#### Phase 31 — Reboot-Spanning Boot ETW

**Goal:** Measure the *next* boot, not only post-boot event logs.

**Status:** Done

**Deliver:**

- Arm ETW Autologger for next reboot (`wintune boot arm`); status / disarm.
- After reboot, `boot analyze` / `scan` / `doctor` auto-pick the reboot `.etl`
  under `%ProgramData%\WinTune\traces\` and stop the session.
- Summarized timeline remains event-log-first; ETL is event-count supplement
  (never a raw dump).

**Commands:**

```bash
wintune boot arm
wintune boot status
# reboot ...
wintune boot analyze
wintune boot disarm
```

**Depends on:** Phase 10.

**Never:** Raw unreadable `.etl` dumps as the primary UX.

---

#### Phase 32 — Cold vs Warm Boot Profiles

**Goal:** Classify recent boots and recommend from patterns.

**Status:** Done

**Deliver:**

- Cold vs warm/hybrid labels from Event 100 `BootKernelInitTime` (Fast Startup
  typically shows tiny kernel init; full power-off cold boots do not).
- Multi-boot history (up to 8 Event 100 samples): averages, slow/degraded
  counts, per-entry kind + timestamp.
- `WT-BOOT-001` requires a sustained pattern (≥2 slow boots or multi-sample
  average ≥ 60 s) — a single slow boot no longer alarms.
- Surfaced in `boot analyze`, scan/doctor text, and JSON `boot.history`.

**Depends on:** Phase 31 (or Phase 10 event history where enough).

---

#### Phase 33 — Driver / Service Start Waterfall

**Goal:** Ordered timeline of slow SCM/driver starts during boot.

**Status:** Done

**Deliver:**

- Collect Diagnostics-Performance events **101–110** for the latest boot
  (apps, drivers, services, devices, prefetch, policy, …).
- Waterfall summary in `boot analyze`: sorted by impact (duration), with
  optional start offset from `BootStartTime`, event id, and SCM match.
- Correlate component names with Win32 services (`service_name`, state, PID)
  when display/service names match.
- JSON: `components[]` gains `event_id`, `start_offset_ms`, `service_*`;
  `waterfall_total_ms` sums attributed delays.

**Depends on:** Phases 10, 31.

---

#### Phase 34 — Startup Delay Orchestration

**Goal:** Safe staggered delays for user-approved items.

**Status:** Done

**Deliver:**

- `wintune startup delay-plan` — preview staggered delays (base + per-item
  stagger); shows protected Microsoft security items as SKIP.
- `wintune startup delay-plan apply` — per-item confirmation (or `--yes`);
  uses existing `startup delay` / `tasks delay` + rollback records.
- `--include-tasks` adds logon/boot third-party tasks; `--seconds N` sets base.
- Never blind delay-all; never auto-delay Microsoft security / health startups.

**Depends on:** Phases 12, 16.

**Never:** Blind delay-all; delay Microsoft security startups by default.

---

### Recommendations & apply

#### Phase 35 — Recommendation Confidence Engine

**Goal:** Confidence from sample count, variance, and duration.

**Status:** Done

**Deliver:**

- Shared `wt_confidence_compute` / `wt_confidence_should_emit` (sample count,
  scan window duration, peak−avg spread).
- Multi-sample CPU/memory/disk stats on the scan report for variance inputs.
- Suppress noisy single-sample CPU/GPU/disk (unless extreme ≥ 95%).
- Recommendations expose `confidence_percent` + `confidence_basis` in text/JSON;
  scan JSON includes a `confidence` summary object.

**Depends on:** Phase 4.

---

#### Phase 36 — Apply Preview / Dry-Run

**Goal:** Show exact change + rollback payload before mutating.

**Status:** Done

**Deliver:**

```bash
wintune apply WT-POWER-001 --dry-run
wintune startup disable <id> --dry-run
wintune power --set performance --dry-run
```

- Shared `WT_ApplyPreview` + `wt_apply_preview_*` (read-only; never writes
  system state or rollback files).
- Text and JSON preview of `would_mutate`, previous/new values, admin need.
- `--dry-run` / `--preview` on apply, startup enable/disable/delay, and
  `power --set`.

**Depends on:** Phase 16.

**Never:** Dry-run that still writes system state.

---

#### Phase 37 — Service Restart Rollback Metadata

**Goal:** Record pre-restart service state for audit/rollback notes.

**Status:** Done

**Deliver:**

- Successful `services restart` writes a `service_restart` audit record with
  pre/post state and PID (`name|state|pid`).
- `rollback apply` on that type explains audit-only behavior and does not
  stop/start again (no restart loops).
- Expanded critical/security denylist (Defender, firewall, WU, SCM core, …).

**Depends on:** Phases 7, 16.

**Never:** Auto-restart loops; restart Defender/WU/firewall/etc.

---

#### Phase 38 — Guided Doctor Plan

**Goal:** Ordered checklist of safe applies with dependencies.

**Status:** Done

**Deliver:**

- `doctor` appends a sequenced Guided Doctor Plan (APPLY vs REVIEW).
- `doctor --plan` / `doctor --plan --json` for plan-focused output.
- Dependencies: blockers → power → resources → startup → tasks.
- User still confirms each mutating step (no blind apply-all).

**Depends on:** Phases 4, 35, 36.

**Never:** One-shot “fix everything” with `--yes` for dangerous sets.

---

#### Phase 39 — Uninstall Advisor (Read-Only)

**Goal:** Point humans at official uninstall paths for high-impact leftovers.

**Status:** Done

**Deliver:**

- `wintune apps` — read-only ARP/Uninstall registry scan.
- Candidates from high-impact startup correlation and/or large third-party size.
- Guidance only: Settings / `winget list` / ARP — never runs UninstallString.
- `WT-UNINSTALL-001` advisory recommendation when candidates exist.

**Never:** Silent uninstall; force-remove Program Files.

---

### TUI / tray / UX

#### Phase 40 — TUI Historical Sparklines

**Goal:** Short in-session history for CPU/RAM/disk/net.

**Status:** Done

**Deliver:**

- 24-sample ring buffer (CPU/RAM/disk % + net rx/tx B/s).
- Sparklines: `cpu~` / `ram~` / `dsk~` / `dn~` / `up~` (compact theme skips).
- Export `e` → `.txt` + `.json` + `.csv` including history.

**Depends on:** Phase 20.

---

#### Phase 41 — TUI Before/After Compare

**Goal:** Side-by-side snapshots after a confirmed apply.

**Status:** Done

**Deliver:**

- Store/load two snapshots under `%LOCALAPPDATA%\WinTune\compare\{before|after}.json`.
- TUI keys: `b` mark before, `a` mark after, `c` compare view.
- Show measured deltas (percentage points / B/s) with calm “sample-window only” wording.
- Text `report` includes the pair when snapshots exist on disk.

**Depends on:** Phases 20, 8.

**Never:** Imply magical gains without measured deltas.

---

#### Phase 42 — Tray Mini-Doctor

**Goal:** Tray runs a short local scan into the status window.

**Status:** Done

**Deliver:**

- Optional **Quick scan** from tray menu and status window button.
- In-process short scan (2 samples) + recommendations shown in the status window.
- CLI apply hints only; never mutates from the tray.

**Depends on:** Phase 19.

**Never:** Silent mutating actions from the tray.

---

#### Phase 43 — Accessibility & SSH TUI

**Goal:** Better narrow-TTY, high-contrast, and clearer labels.

**Status:** Done

**Deliver:**

- `--theme high-contrast` (`hc`): bright utilization colors, ASCII chrome, a11y labels.
- `--theme ssh`: ASCII, no color, compact layout, clearer labels.
- `--safe-terminal` / remote: ASCII + compact + a11y; optional HC colors over SSH.
- Narrower minimum size (36×12) and shorter key footer in safe layouts.

**Depends on:** Phases 9, 20.

**Never:** Require mouse or Windows Terminal-only features.

---

### Service / fleet / automation

#### Phase 44 — Service Policy Profiles

**Goal:** Named configs for interval, counters, retention of `last_scan.json`.

**Status:** Done

**Deliver:**

```bash
wintune service install --profile balanced
wintune service set-profile performance
wintune service profile
```

- Profiles: `balanced`, `performance`, `light`, `on-demand`
- Policy file: `%ProgramData%\WinTune\service_policy.json` (removed on uninstall)
- Rotates `last_scan.N.json` per `history_keep`

**Depends on:** Phase 11.

**Never:** Hidden persistence; profiles must be explicit and removable.

---

#### Phase 45 — Named-Pipe ACL Hardening

**Goal:** Tighten local IPC trust boundaries.

**Status:** Done

**Deliver:**

- Documented ACLs: `admin` (default) and `admin-only` (stricter deny ACEs).
- Config: `%ProgramData%\WinTune\pipe_acl.json` (removed on uninstall).
- CLI: `service install --pipe-acl …`, `service set-pipe-acl …`, status shows mode.
- Clear `WT_ERR_ACCESS_DENIED` + user message when ACL rejects callers.

**Depends on:** Phase 11.

---

#### Phase 46 — Fleet Report Pack

**Goal:** Bundle multi-host JSON scans locally for review.

**Status:** Done

**Deliver:**

```bash
wintune fleet pack --input reports/ --output fleet-pack.zip
```

- Local ZIP (`manifest.json`, `CHECKSUMS.sha256`, `reports/*`); SHA-256 per file and of the ZIP.
- CLI: `--input`, `--output` (default `fleet-pack.zip`); `--json` result document.
- Unit tests: CRC-32, SHA-256, ZIP STORE signature.

**Depends on:** Phase 17.

**Never:** Cloud sync or mandatory phone-home.

---

#### Phase 47 — JSON Schema v2 + Compatibility

**Goal:** Evolve JSON without breaking automation casually.

**Status:** Done

**Deliver:**

- Default `schema_version` **`2.0.0`** with `schema_compat_min` + `document`.
- Changelog of additive fields since 1.0; deprecation window via
  `--schema-version 1` (exact `1.0.0` envelope pin).
- Compatibility notes in `docs/json-schema.md` / `docs/fleet.md` /
  `docs/json-changelog.md`.

**Depends on:** Phase 17.

---

### Windows platform diagnostics (read-heavy)

#### Phase 48 — Storage Health (Read-Only)

**Goal:** Reliability / failure-prediction signals when available.

**Status:** Done

**Deliver:**

- `wintune storage` / `--json`: physical disk identity + failure prediction.
- Advisory `WT-DISK-005` when prediction reports failure (backup + vendor tools).
- APIs: `IOCTL_STORAGE_PREDICT_FAILURE`, `IOCTL_STORAGE_QUERY_PROPERTY`.

**Never:** Disk wipe, format, “repair” that deletes user data.

---

#### Phase 49 — Memory Dump / WER Signals

**Goal:** Explain slowness after crashes via recent WER / unexpected shutdowns.

**Status:** Done

**Deliver:**

- `wintune reliability` / `--json`: System/Application Event Log summary (14-day
  lookback) + local dump/WER **metadata only**.
- Advisories: `WT-RELIABILITY-001` … `003`.

**Never:** Exfiltrate dumps; claim to “fix” corruption automatically.

---

#### Phase 50 — Pagefile & Commit Charge Depth

**Goal:** Explain memory pressure without a RAM cleaner.

**Deliver:**

- Commit limit/peak, hard faults; tips to close/delay heavy apps.

**Never:** Force-empty working sets as an “optimization.”

---

#### Phase 51 — Scheduled Maintenance Windows

**Goal:** Detect Defender/WU/optimization activity overlapping high samples.

**Deliver:**

- Correlate high disk/CPU samples with known maintenance tasks.
- Recommend scheduling outside work hours — not disabling security.

**Depends on:** Phases 12, 13, 15.

**Never:** Disable Defender or Windows Update.

---

### Distribution & quality

#### Phase 52 — Authenticode CI + Release Signing

**Goal:** Optional signed Release artifacts with verify-in-smoke.

**Deliver:**

- CI/OIDC or documented signing path; `signtool verify` in release checks.
- See [`signing.md`](signing.md).

**Depends on:** Phase 21.

**Never:** Commit private keys to the repo.

---

#### Phase 53 — Installer & Channel Maturity

**Goal:** First-class installer + channels beyond portable ZIP.

**Deliver:**

- Wire Inno (or MSI) into release workflow when ready.
- Stable/beta channel docs; optional Chocolatey later.
- Keep portable ZIP as the primary artifact.

**Depends on:** Phases 21, 52 (signing recommended before wide installer push).

**Never:** Bundled adware; silent auto-start without consent.

---

## Recommended Implementation Order

### Historical (Phases 10–21) — complete

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

### Suggested (Phases 22–53)

```text
35  Recommendation confidence
30  Multi-sample disk smoothing
25  Disk counters in scan model
26  Per-volume disk activity
22  Publisher & signature metadata
23  Binary provenance
24  Smart impact scoring v2
31  Reboot-spanning boot ETW
32  Cold vs warm boot profiles
33  Driver/service start waterfall
36  Apply dry-run
37  Service restart rollback metadata
38  Guided doctor plan
34  Startup delay orchestration
40  TUI historical sparklines
41  TUI before/after compare
42  Tray mini-doctor
43  Accessibility & SSH TUI
27  Per-process network (ETW)        # optional / overhead-sensitive
28  GPU readiness (read-only)        # if APIs are clean enough
29  Thermal & power budget
48  Storage health
49  WER / unexpected shutdown signals
50  Pagefile & commit depth
51  Maintenance window correlation
39  Uninstall advisor (read-only)
44  Service policy profiles
45  Named-pipe ACL hardening
46  Fleet report pack
47  JSON schema v2
52  Authenticode CI
53  Installer & channel maturity
```

Strong dependencies: **22 before 23/24**; **25 before 26**; **10 before 31–33**;
**16 before 36–38**; **21 before 52–53**.

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
