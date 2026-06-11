# WinTune Roadmap

WinTune ships in phases. Each phase builds on a stable core scanner. v1 is
CLI-first and native; later phases add the TUI, safe apply actions, reports,
SSH hardening, and (eventually) ETW tracing and a background service.

Initial version: **0.1.0** (semantic versioning).

---

## Phase 0 — Skeleton

- CMake project and `wintune.exe`.
- `wintune help`, `wintune version`.
- Logging and error handling.
- Basic module structure.

```bash
wintune help
wintune version
```

---

## Phase 1 — Basic Metrics

- CPU total usage.
- Memory usage.
- Disk free space.
- Top processes by memory.
- Basic text output.

```bash
wintune scan
wintune top
```

---

## Phase 2 — JSON and Automation

- Small, safe JSON emitter.
- `--json` for scan/top.
- Stable report model.

```bash
wintune scan --json
wintune top --json
```

---

## Phase 3 — Watch Mode

- Refreshing process table without the full TUI.
- Configurable interval.

```bash
wintune top --watch
```

---

## Phase 4 — Recommendations

- Recommendation engine.
- Power plan, memory pressure, disk pressure, and startup review
  recommendations.

```bash
wintune recommend
wintune doctor
```

---

## Phase 5 — Startup and Services

- Registry startup scanner.
- Startup folder scanner.
- Auto-start service scanner.
- Service status listing.

```bash
wintune startup
wintune services
```

---

## Phase 6 — Terminal Dashboard / TUI

- `wintune tui` with an ANSI/VT renderer.
- Unicode mode, ASCII fallback, no-color, safe-terminal.
- Live CPU/RAM/disk/network/process view.

```bash
wintune tui
wintune tui --safe-terminal
wintune tui --no-unicode
```

---

## Phase 7 — Safe Apply Actions

- Switch power plan.
- Restart a selected service.
- Disable a user-approved startup entry.
- Rollback metadata.

```bash
wintune apply <id>
wintune power --set performance
```

---

## Phase 8 — Reports

- Text report.
- JSON report.
- Output to file.

```bash
wintune report --format text
wintune report --format json
```

---

## Phase 9 — SSH Hardening

- Better non-interactive detection.
- Better safe-terminal rendering.
- Better JSON remote behavior.
- Clear admin-required messages over SSH.

```bash
ssh user@host "wintune scan"
ssh user@host "wintune scan --json"
ssh user@host "wintune top --watch"
```

---

## Phase 10 — ETW Boot Analysis (later)

- Boot trace collection.
- Login trace collection.
- Slow startup attribution.
- Disk-heavy startup detection.

Not started until the core scanner is stable.

---

## Phase 11 — Background Agent / Windows Service (later)

- Windows Service.
- Periodic scans.
- Local named-pipe interface.
- Privileged local actions.
- Scheduled recommendations.

The service must never become hidden persistence: clearly installed, clearly
removable, and documented.

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

---

## Implementation Order (from an empty repo)

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
