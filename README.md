# WinTune

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: Windows 10 | 11](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?logo=windows&logoColor=white)](#supported-platforms)
[![Language: C17](https://img.shields.io/badge/Language-C17-00599C?logo=c&logoColor=white)](#build)
[![Build: CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake&logoColor=white)](#build)
[![Compiler: MSVC](https://img.shields.io/badge/Compiler-MSVC-5C2D91?logo=visualstudio&logoColor=white)](#build)
[![Version: 0.1.0](https://img.shields.io/badge/Version-0.1.0-blue.svg)](docs/roadmap.md)
[![Status: WIP](https://img.shields.io/badge/Status-WIP%20(Phase%202)-orange.svg)](#project-status)

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

CPU:
  Usage: 7.7%
  Logical processors: 32

Memory:
  Used: 21.6 GB / 63.9 GB (33.8%)

Disk:
  C:\ 272.8 GB free / 892.8 GB (30.6% free)

Top Processes by Memory:
PID      Process                            Memory      Private
13136    Cursor.exe                         1.1 GB       1.1 GB
24628    Discord.exe                      606.7 MB     533.5 MB
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
```

Common options for `scan`/`top` today:

```powershell
wintune scan --interval 1000   # CPU sampling interval (ms)
wintune top --limit 15         # number of rows
```

Full command reference: [`docs/cli.md`](docs/cli.md).

### JSON automation

`scan` and `top` support `--json`, emitting only JSON on stdout so output can
be piped and parsed safely. Use `--output <path>` to write to a file instead
(UTF-8, no BOM):

```powershell
wintune scan --json
wintune scan --json --output report.json
wintune top  --json --limit 20
```

The scan document has a stable shape:

```json
{
  "version": "0.1.0",
  "timestamp_utc": "2026-06-11T08:00:13Z",
  "system":  { "available": true, "os": "Windows 11 Pro", "arch": "x64", "hostname": "...", "uptime_ms": 0 },
  "cpu":     { "available": true, "logical_processors": 32, "total_usage_percent": 14.1 },
  "memory":  { "available": true, "total_bytes": 0, "available_bytes": 0, "used_bytes": 0, "used_percent": 35.3 },
  "disk":    { "available": true, "volumes": [ { "root": "C:\\", "total_bytes": 0, "free_bytes": 0, "free_percent": 30.5 } ] },
  "processes": { "available": true, "top": [ { "pid": 0, "name": "...", "working_set_bytes": 0, "private_bytes": 0, "read_bytes": 0, "write_bytes": 0, "cpu_percent": null } ] },
  "recommendations": []
}
```

Each section carries an `available` flag so partial failures are visible
rather than fatal. `cpu_percent` is `null` until per-process CPU lands in a
later phase.

### Over SSH

WinTune works through the Windows OpenSSH Server as a normal terminal program:

```bash
ssh user@windows-host "wintune scan"
ssh user@windows-host "wintune scan --json"
ssh user@windows-host "wintune top --watch"
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
| 3 | `top --watch` live refresh | Planned |
| 4 | Recommendation engine, `recommend`, `doctor` | Planned |
| 5 | `startup`, `services` | Planned |
| 6 | `tui` dashboard | Planned |
| 7 | Safe apply actions (power plan, etc.) | Planned |
| 8 | `report` (text + JSON) | Planned |
| 9 | SSH hardening | Planned |

Commands that are recognized but not yet implemented print a clear notice and
exit non-zero. The full plan is in [`docs/roadmap.md`](docs/roadmap.md).

### Current limitations

- `--json` and `--output` work for `scan` and `top`; `--output` for text
  reports arrives with `report` (Phase 8).
- `top --watch` currently prints a single snapshot (Phase 3).
- Per-process CPU and disk-rate columns are not yet computed (later phases);
  `cpu_percent` is reported as `null` in JSON for now.
- Recommendations, startup/service inspection, power control, and the TUI are
  not implemented yet.

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
