# WinTune milestones 54–157

Phases 0–64 are done. See [`roadmap.md`](roadmap.md). Phases 65–157 are
**Planned**.

`scan` and unpaged `top` keep a top-K process cap of 32. `top --page` walks
the full list. Six recommendation ids can be applied, and the rest are
advisory. The Windows service stays local.

**Never, for every phase in this file:**

- Disable Microsoft Defender, Windows Update, the firewall, UAC, or BitLocker.
- Delete user files, clean the registry, or uninstall software automatically.
- Upload a report, add telemetry, or phone home. Phase 157 is the only
  exception, and it is off by default: an opt-in version check that sends no
  machine data.
- Apply a change from the TUI. The dashboard stays read-only.
- Add a kernel driver, inject into a process, or hook APIs.

---

## Measure (54–73)

### Phase 54 — Full process list

**Status:** Done

**Deliver:** Page the full process list. `scan` and `top` keep a top-K cap of 32.
`wintune top --page <n>` walks every enumerated process. `--page-size` defaults
to 32 and cannot exceed 32. `scan` still stores at most 32 processes.

### Phase 55 — Process identity

**Status:** Done

**Deliver:** Show parent PID, session, and integrity level on each process row.
`parent_pid` comes from ToolHelp. `session_id` is null when Windows will not
report the session. `integrity` is `unknown` when the process token cannot be
opened. Nothing here changes a token or a process.

### Phase 56 — Hung processes

**Status:** Done

**Deliver:** Detect hung processes with the official wait-chain API. Read-only.
`GetThreadWaitChain` runs only on the rows `top` and `scan` already return.
`hung` is a `SendMessage` block or a deadlock cycle. Ordinary waits are not
hangs. WinTune does not end the process.

### Phase 57 — Per-process GPU

**Status:** Done

**Deliver:** Add per-process GPU engine time next to the existing adapter inventory.
Each hardware adapter lists up to 8 processes. `engine_percent` is the sum of
that process's `\GPU Engine(*)\Utilization Percentage` instances on that
adapter during the sample, clamped to 100. Read-only.

### Phase 58 — UDP rates

**Status:** Done

**Deliver:** Sample UDP byte rates beside the TCP EStats already collected.
`--include-network` (and `--sort network`) starts a short real-time
Microsoft-Windows-TCPIP trace for summary events 1169 (send bytes) and 1170
(deliver bytes) during the same window as TCP Extended Stats. JSON adds
`udp_recv_bytes_per_sec` and `udp_send_bytes_per_sec` (null when the trace
did not start). Text tables add UDP R / UDP W. Addresses and payloads are
not stored. The trace needs an administrator; without one, TCP rates still
work and the UDP fields stay null. A process with no summary events in the
window is 0, not null.

### Phase 59 — Disk latency

**Status:** Done

**Deliver:** Report disk latency percentiles beside active percent and queue
length. The same PDH window reads `\PhysicalDisk(_Total)\Avg. Disk sec/Read`
and `Avg. Disk sec/Write`, and the matching `LogicalDisk` counters per fixed
volume. Values are milliseconds. `read_latency_ms_p50` / `p95` and
`write_latency_ms_p50` / `p95` are nearest-rank percentiles of those sample
windows. Each window is already an average, so this is not a per-I/O
histogram. One sample makes p50 and p95 the same number. A missing counter
is JSON null. Read-only.

### Phase 60 — Storage sensors

**Status:** Done

**Deliver:** Read storage temperature and SMART health where the public API
allows it. `StorageDeviceTemperatureProperty` supplies Celsius, plus the
warning and critical thresholds when the device reports them.
`StorageDeviceProtocolSpecificProperty` reads NVMe health log page 02h:
spare percent, spare threshold, percentage used, and the critical-warning
byte. Temperature falls back to that log when the property is absent.
SATA drives that do not implement the log keep those fields null. WinTune
does not send ATA passthrough and does not change thresholds. A predicted
failure stays `degraded`. A crossed temperature or NVMe threshold is
`warning` and can raise advisory `WT-DISK-006`. Read-only.

### Phase 61 — Power mode

**Status:** Done

**Deliver:** Record the Windows 11 power mode and battery drain rate. Do not
change them yet. `PowerGetUserConfiguredACPowerMode` and
`PowerGetUserConfiguredDCPowerMode` supply the slider for AC and battery
(`best_efficiency`, `balanced`, `better_performance`, `best_performance`).
The lookup is resolved at runtime, so a Windows 10 build leaves the fields
null. The signed battery rate from `SYSTEM_BATTERY_STATE` is shown whenever
it is measured, as a discharge or a charge. `wintune power --set` still
changes only an existing power plan. The power mode is not written.

### Phase 62 — Memory lists

**Status:** Done

**Deliver:** Expose memory compression, standby, and modified-page sizes.
Standby is the sum of the three public cache counters (reserve, normal
priority, and core). Modified is `\Memory\Modified Page List Bytes`.
Compression is `\Process(Memory Compression)\Working Set`, the compressed
store’s physical size. A missing counter leaves that JSON field null.
Read-only. WinTune does not empty working sets.

### Phase 63 — Handle and GDI counts

**Status:** Done

**Deliver:** Add handle count and GDI count to the top-process table.
`GetProcessHandleCount` and `GetGuiResources(GR_GDIOBJECTS)` run on the
process handle already opened for memory counters. A failed read is `-` in
text and JSON null. Zero is a real count. Read-only. WinTune does not close
handles or GDI objects.

### Phase 64 — DPC and interrupts

**Status:** Done

**Deliver:** Name the loudest DPC or interrupt source when CPU is high and
user processes are quiet. DPC time and interrupt time come from the same
PDH window as total CPU. A short real-time kernel trace
(`EVENT_TRACE_FLAG_DPC`, `EVENT_TRACE_FLAG_INTERRUPT`, and image load) runs
only when total CPU is at least 50%, DPC plus interrupt time is at least
10%, and user processes stay under 15% each and 25% combined. The result is
a file name such as `ndis.sys`, plus `dpc` or `interrupt`. JSON
`kernel_source` is null otherwise, including when the trace needs an
administrator. Read-only. No driver is installed.

### Phase 65 — Displays

**Status:** Planned

**Deliver:** Inventory monitors, refresh rate, and HDR state as display readiness.

### Phase 66 — Audio endpoints

**Status:** Planned

**Deliver:** Flag exclusive-mode audio endpoints as a latency hint.

### Phase 67 — Search indexer

**Status:** Planned

**Deliver:** Report Search indexer backlog without offering to disable Search.

### Phase 68 — Defender status

**Status:** Planned

**Deliver:** Show Defender real-time status as a fact. Never add a disable action.

### Phase 69 — Per-adapter network

**Status:** Planned

**Deliver:** Split network throughput by adapter, including VPN and virtual switches.

### Phase 70 — Short-lived processes

**Status:** Planned

**Deliver:** Track short-lived processes across the sample window so they are not missed.

### Phase 71 — Collector timing

**Status:** Planned

**Deliver:** Add a tool self-timing line: how long each collector took.

### Phase 72 — PDH query reuse

**Status:** Planned

**Deliver:** Cache PDH query handles inside one scan instead of opening them per sample.

### Phase 73 — Counter catalog

**Status:** Planned

**Deliver:** Document every counter, its unit, and the API in [`metrics.md`](metrics.md) from the code.

---

## Explain (74–83)

### Phase 74 — Numbers in reasons

**Status:** Planned

**Deliver:** Put the measured number in every recommendation reason, not only the prose.

### Phase 75 — Confidence in text

**Status:** Planned

**Deliver:** Print the confidence basis on the text report, not only inside JSON.

### Phase 76 — Doctor groups

**Status:** Planned

**Deliver:** Group doctor output into measured, advisory, and applyable.

### Phase 77 — Next command

**Status:** Planned

**Deliver:** Name the single next command at the end of `doctor`.

### Phase 78 — Duplicate startup

**Status:** Planned

**Deliver:** Suppress duplicate startup findings when the same binary is Run, folder, and task.

### Phase 79 — Publisher on findings

**Status:** Planned

**Deliver:** Attach publisher and signature to startup and task recommendations.

### Phase 80 — Why confidence is low

**Status:** Planned

**Deliver:** Explain a low score: one sample, missing boot log, or access denied.

### Phase 81 — Command help

**Status:** Planned

**Deliver:** Add `wintune help <command>` with flags, exit codes, and whether it mutates.

### Phase 82 — Help sections

**Status:** Planned

**Deliver:** Split `wintune help` into read-only commands and commands that change the system.

### Phase 83 — Error next step

**Status:** Planned

**Deliver:** Include the Win32 or PDH code plus the next step in every error.

---

## Apply (84–97)

### Phase 84 — Apply startup impact

**Status:** Planned

**Deliver:** Route `WT-STARTUP-001` through the existing StartupApproved disable path.

### Phase 85 — Apply startup delay

**Status:** Planned

**Deliver:** Route `WT-STARTUP-002` through the existing delay path.

### Phase 86 — Delay-plan batch

**Status:** Planned

**Deliver:** Apply a startup delay-plan as one confirmed batch, still one entry at a time inside.

### Phase 87 — Doctor next step

**Status:** Planned

**Deliver:** Add `wintune doctor --plan --next` to apply only the first reversible step.

### Phase 88 — Startup confirm detail

**Status:** Planned

**Deliver:** Show publisher, path, and measured impact in the startup confirm prompt.

### Phase 89 — Service dependents

**Status:** Planned

**Deliver:** Refuse a service restart when stopping it would stop dependents.

### Phase 90 — Rollback last

**Status:** Planned

**Deliver:** Add `wintune rollback last` for the newest record.

### Phase 91 — Power mode change

**Status:** Planned

**Deliver:** Write a reversible power-mode change, separate from the classic power scheme.

### Phase 92 — Snooze a finding

**Status:** Planned

**Deliver:** Let the user snooze one recommendation id in a local file. No system change.

### Phase 93 — Dry-run JSON

**Status:** Planned

**Deliver:** Emit the dry-run payload as JSON, including the rollback body.

### Phase 94 — Advisory stay advisory

**Status:** Planned

**Deliver:** Keep memory, disk, CPU, boot, update, and uninstall recommendations advisory.

### Phase 95 — Safety regression test

**Status:** Planned

**Deliver:** Test that `--yes` still cannot disable Defender, Windows Update, or the firewall.

### Phase 96 — Confirmation record

**Status:** Planned

**Deliver:** Record who confirmed an action: interactive user or `--yes`.

### Phase 97 — Missing apply target

**Status:** Planned

**Deliver:** Fail `apply` when the target id is missing instead of guessing the first startup entry.

---

## History (98–105, 114)

### Phase 98 — Scan history

**Status:** Planned

**Deliver:** Store the last N scan reports under `%LOCALAPPDATA%\WinTune\history`.

### Phase 99 — Baseline

**Status:** Planned

**Deliver:** Add `wintune baseline save` and `wintune baseline diff`.

### Phase 100 — Report diff

**Status:** Planned

**Deliver:** Add `wintune diff` for two JSON report files.

### Phase 101 — New findings

**Status:** Planned

**Deliver:** Show which recommendation ids are new since the previous scan.

### Phase 102 — Boot duration chart

**Status:** Planned

**Deliver:** Chart boot duration from the cold and warm history already collected.

### Phase 103 — Retention

**Status:** Planned

**Deliver:** Expire history and rollback files with a documented retention command.

### Phase 104 — Text trend line

**Status:** Planned

**Deliver:** Put a one-line trend in the text report: CPU, memory, disk versus last scan.

### Phase 105 — History in a fleet pack

**Status:** Planned

**Deliver:** Let `fleet pack` include the history folder when the user passes a flag.

### Phase 114 — TUI error log

**Status:** Planned

**Deliver:** Write a local session log when the TUI exits on an error.

---

## TUI (106–113, 115)

### Phase 106 — Sort processes

**Status:** Planned

**Deliver:** Sort the process table by CPU, memory, disk, or name.

### Phase 107 — Filter processes

**Status:** Planned

**Deliver:** Filter the process table by a typed substring.

### Phase 108 — Pause refresh

**Status:** Planned

**Deliver:** Pause and resume refresh without leaving the dashboard.

### Phase 109 — Recommendation panel

**Status:** Planned

**Deliver:** Show the current recommendations in a read-only panel.

### Phase 110 — Remember theme

**Status:** Planned

**Deliver:** Remember the last theme in a local settings file.

### Phase 111 — Low-update mode

**Status:** Planned

**Deliver:** Add a low-update mode that refreshes counters less often over SSH.

### Phase 112 — Copy a row

**Status:** Planned

**Deliver:** Copy the selected row as plain text.

### Phase 113 — Complete key list

**Status:** Planned

**Deliver:** List every key in the help overlay, including theme and compare.

### Phase 115 — TUI stays read-only

**Status:** Planned

**Deliver:** Keep apply out of the TUI. Link to the CLI command instead.

---

## CLI (116–124)

### Phase 116 — JSON coverage note

**Status:** Planned

**Deliver:** Correct the README: JSON already exists beyond `scan`, `top`, `doctor`, and `report`.

### Phase 117 — Fail on severity

**Status:** Planned

**Deliver:** Add optional `--fail-on high` so scripts can fail when `doctor` finds high severity.

### Phase 118 — Watch as NDJSON

**Status:** Planned

**Deliver:** Stream `top --watch --ndjson` as one JSON document per refresh.

### Phase 119 — Local defaults

**Status:** Planned

**Deliver:** Read default samples, interval, and theme from a local config file.

### Phase 120 — Shell completions

**Status:** Planned

**Deliver:** Ship PowerShell and bash completion scripts.

### Phase 121 — Dead command branch

**Status:** Planned

**Deliver:** Delete the dead "recognized but not implemented" branch or point it at real gaps.

### Phase 122 — Atomic output files

**Status:** Planned

**Deliver:** Make `--output` atomic: write a temp file, then replace the destination.

### Phase 123 — Log line format

**Status:** Planned

**Deliver:** Document a stable log line format for `--log-file`.

### Phase 124 — Command inventory

**Status:** Planned

**Deliver:** Add `wintune commands --json` listing name, mutates, and admin.

---

## Service (125–132)

### Phase 125 — Cache age

**Status:** Planned

**Deliver:** Show scan-cache age in `wintune service status`.

### Phase 126 — No self-apply

**Status:** Planned

**Deliver:** State in status that the service never applies changes by itself.

### Phase 127 — Policy cannot widen apply

**Status:** Planned

**Deliver:** Add a test that a policy profile cannot enable an advisory id.

### Phase 128 — ACL denied message

**Status:** Planned

**Deliver:** Print the pipe ACL mode when a client gets access denied.

### Phase 129 — Service repair

**Status:** Planned

**Deliver:** Add `wintune service repair` to check binary path, state, and pipe reachability.

### Phase 130 — Moved binary

**Status:** Planned

**Deliver:** Update the service binary path when the exe moves.

### Phase 131 — Uninstall scope

**Status:** Planned

**Deliver:** On uninstall, remove the service and the ACL file. Leave rollback unless asked.

### Phase 132 — Local service log

**Status:** Planned

**Deliver:** Write service failures to the local WinTune log, not to a remote endpoint.

---

## Boot (133–140)

### Phase 133 — Cold and warm medians

**Status:** Planned

**Deliver:** Print cold versus warm medians in `boot analyze`.

### Phase 134 — Waterfall sort

**Status:** Planned

**Deliver:** Sort the driver and service waterfall by duration.

### Phase 135 — Unsigned boot drivers

**Status:** Planned

**Deliver:** Mark unsigned boot drivers as a read-only finding. Do not remove them.

### Phase 136 — Login versus boot

**Status:** Planned

**Deliver:** Separate login delay from firmware and kernel time in the doctor summary.

### Phase 137 — Boot state in the tray

**Status:** Planned

**Deliver:** Show boot arm, status, and disarm state in the tray tooltip.

### Phase 138 — Boot confidence

**Status:** Planned

**Deliver:** Use boot history count as the confidence input for `WT-BOOT-001`.

### Phase 139 — Trace session cleanup

**Status:** Planned

**Deliver:** Guarantee `boot trace` never leaves a session running after an error.

### Phase 140 — Elevation hint

**Status:** Planned

**Deliver:** Tell the user when the Diagnostic log needs elevation, with the exact command.

---

## Fleet (141–148)

### Phase 141 — Schema check

**Status:** Planned

**Deliver:** Validate each input report against schema 2 before packing.

### Phase 142 — Mixed schema

**Status:** Planned

**Deliver:** Reject a pack that mixes schema 1 and schema 2, and name the file.

### Phase 143 — Host CSV

**Status:** Planned

**Deliver:** Add a CSV host summary next to the zip.

### Phase 144 — Redact

**Status:** Planned

**Deliver:** Add a redact flag that strips hostname and username from a shared report.

### Phase 145 — Pack diff

**Status:** Planned

**Deliver:** Compare two fleet packs and list hosts whose high findings changed.

### Phase 146 — No upload

**Status:** Planned

**Deliver:** Keep pack local. Do not add an upload command.

### Phase 147 — Automation examples

**Status:** Planned

**Deliver:** Update the SSH and Ansible examples for schema 2 and `--fail-on`.

### Phase 148 — Exit-code test

**Status:** Planned

**Deliver:** Make the documented exit-code table a test against `exit_codes.c`.

---

## Release (149–153)

### Phase 149 — WinGet hash script

**Status:** Planned

**Deliver:** Generate the winget YAML hashes from the release zip in one script.

### Phase 150 — Static CRT gate

**Status:** Planned

**Deliver:** Fail that script if `verify-portable-exe.ps1` finds `VCRUNTIME140.dll`.

### Phase 151 — Signing in the release notes

**Status:** Planned

**Deliver:** Write `SIGNING.txt` contents into the GitHub Release body.

### Phase 152 — Checksum command

**Status:** Planned

**Deliver:** Add a user command that checks a downloaded zip against the `.sha256` sidecar.

### Phase 153 — PATH helper

**Status:** Planned

**Deliver:** Make `Add-To-Path` refuse a duplicate PATH entry.

---

## Follow-ups from the docs (154–157)

These were described in the docs and had no phase.

### Phase 154 — Azure Trusted Signing

**Status:** Planned

**Deliver:** Sign release binaries with Azure Trusted Signing over GitHub
OIDC, before the zip. Do not store a long-lived certificate or client secret
in the repo. Keep the existing PFX path as a fallback. `signtool verify /pa`
stays the release gate. Tenant-specific names stay out of the public
workflow.

### Phase 155 — Winget manifest publish

**Status:** Planned

**Deliver:** After a stable release, submit `packaging/winget/` to
winget-pkgs. Phase 149 fills the hashes. This phase performs the submission.
It does not run on pull requests.

### Phase 156 — Retire schema 1

**Status:** Planned

**Deliver:** Remove the `--schema-version 1` pin after the deprecation window
documented in `json-changelog.md`. Default output stays schema 2.

### Phase 157 — Opt-in update check

**Status:** Planned

**Deliver:** Add an opt-in check for a newer GitHub release. Off by default.
It sends no machine data, scan, or report. `scan`, `doctor`, and the service
do not call it. This is the only network exception in this file.
