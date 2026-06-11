# WinTune

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

### JSON automation (planned)

JSON output (`--json`) is part of Phase 2. When enabled, WinTune emits only
JSON on stdout so it can be piped and parsed safely:

```powershell
wintune scan --json
```

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
| 2 | JSON output for `scan` / `top` | Planned |
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

- `--json` and `--output` are accepted but not yet implemented (Phase 2/8).
- `top --watch` currently prints a single snapshot (Phase 3).
- Per-process CPU and disk-rate columns are not yet computed (later phases).
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
