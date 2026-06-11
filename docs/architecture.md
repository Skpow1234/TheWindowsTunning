# WinTune Architecture

WinTune is a native Windows performance diagnostics and safe optimization
tool written in C (C17/C11). It is **CLI-first**, **TUI-second**, and
**SSH-friendly**. This document describes how the codebase is layered and how
data flows from the operating system to the user.

---

## Design Goals

- Native, lightweight, single `wintune.exe` binary.
- No Electron, Node.js, .NET, Python, Java, Rust, or Go in the core runtime.
- The **core engine never depends on the UI**. CLI, JSON, and TUI are all
  consumers of the same structured data.
- Correctness over cleverness; safety over aggressive optimization.
- Measurement before recommendation; user approval before any change.
- Official Windows APIs over undocumented hacks.

---

## Layered Architecture

WinTune is organized into three conceptual layers plus supporting modules.

```text
+-------------------------------------------------------------+
|                        Interfaces                           |
|   CLI commands   |   JSON output   |   TUI dashboard         |
+-------------------------------------------------------------+
|                       Core Engine                           |
|   scan  |  recommendations  |  report_model                 |
+-------------------------------------------------------------+
|                    Collection Modules                       |
|   metrics/ (cpu, memory, disk, network, process)            |
|   system/  (services, startup, power, registry, os_info)    |
+-------------------------------------------------------------+
|                     Platform + Common                       |
|   platform/ (console, vt, time, paths)                      |
|   common/   (log, error, string_utils, units)               |
+-------------------------------------------------------------+
|                   Windows Native APIs                       |
|   PDH | PSAPI | Tool Help | SCM | PowerProf | IpHlpApi       |
+-------------------------------------------------------------+
```

### 1. Core Engine (`src/core/`)

Pure C modules that orchestrate collection and produce structured data.
They do **not** print to the screen or assume any output format.

- `scan.c/.h` — runs a full system scan, populating `WT_ScanReport`.
- `recommendations.c/.h` — deterministic recommendation engine.
- `report_model.c/.h` — the stable in-memory model shared by all outputs.

### 2. Collection Modules

#### `src/metrics/`
Reads live performance data using PDH and Win32 APIs.

- `cpu.c` — total + per-process CPU using PDH counters.
- `memory.c` — `GlobalMemoryStatusEx`, per-process working set.
- `disk.c` — free space per volume, disk active time, I/O bytes.
- `network.c` — adapter info and throughput.
- `process.c` — process enumeration via Tool Help + PSAPI.
- `pdh_utils.c` — thin, safe wrapper over PDH query lifecycle.

#### `src/system/`
Inspects system configuration.

- `services.c` — Service Control Manager enumeration.
- `startup.c` — registry Run keys + startup folders.
- `power.c` — active scheme and available schemes.
- `registry.c` — safe, read-heavy registry helpers.
- `os_info.c` — OS version, architecture, uptime, hostname.
- `privilege.c` — elevation detection (`wt_is_process_elevated`).

### 3. Interfaces

#### `src/cli/`
Argument parsing and command dispatch. Each command (`scan`, `top`,
`startup`, `services`, `power`, `report`, `doctor`) is its own module and is
fully usable without the TUI.

#### `src/output/`
Rendering of the report model.

- `table.c` — aligned text tables.
- `json.c` — small, safe, dependency-free JSON emitter.
- `text.c` — human-readable text blocks.
- `text_report.c` — full report formatting.

#### `src/tui/`
Optional interactive dashboard built on manual ANSI/VT rendering (no curses).

- `tui.c` — main loop and state.
- `tui_screen.c` — frame rendering.
- `tui_input.c` — non-blocking key input.
- `tui_widgets.c` — bars, tables, panels.
- `tui_theme.c` — color / unicode / ASCII themes.

### 4. Actions (`src/actions/`)

Mutating operations, always gated behind confirmation.

- `apply.c` — apply a recommendation by ID.
- `rollback.c` — write and replay rollback metadata.
- `safe_actions.c` — the allow-list of safe operations.

### 5. Platform + Common

- `platform/console.c`, `vt.c` — VT enablement, terminal size, cursor control.
- `platform/time.c`, `paths.c` — timestamps and well-known data paths.
- `common/log.c`, `error.c`, `string_utils.c`, `units.c` — cross-cutting helpers.

---

## Data Flow

```text
CLI args ──► cli.c (parse) ──► command module
                                   │
                                   ▼
                        core/scan.c (orchestrate)
                                   │
            ┌──────────────────────┼──────────────────────┐
            ▼                      ▼                      ▼
       metrics/*              system/*             recommendations.c
            └──────────────────────┼──────────────────────┘
                                   ▼
                          WT_ScanReport (model)
                                   │
            ┌──────────────────────┼──────────────────────┐
            ▼                      ▼                      ▼
       output/text.c         output/json.c           tui/*
```

The **report model is the contract**. Collection fills it, outputs consume it,
and partial failures are recorded in the model rather than crashing the program.

---

## Error Handling

Every public function returns a structured `WT_Result` (see `common/error.h`).
- Windows API failures preserve `GetLastError()` where useful.
- PDH failures preserve the `PDH_STATUS`.
- Partial failures degrade gracefully: if CPU collection fails but memory
  succeeds, the report is still produced with CPU marked unavailable.

---

## Memory & Handle Ownership

- One memory style per module, used consistently.
- Every Windows handle is closed; every allocation is freed.
- Fixed-size arrays are acceptable for v1 where limits are documented
  (e.g. `top_processes[64]`, `volumes[32]`).

---

## What the Architecture Deliberately Excludes (v1)

- No kernel drivers, no process injection, no API hooking.
- No background service (designed for, but not built in v1).
- No GUI, no web dashboard, no embedded browser runtime.
- No cloud, telemetry, or network calls.
