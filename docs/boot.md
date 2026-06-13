# Boot and Login Analysis (Phase 10)

WinTune reads **Windows Diagnostic-Performance** data — the same ETW-backed
event log Windows uses for boot and login performance monitoring. Output is
**summarized** for humans and JSON; WinTune never dumps raw ETW events to the
terminal.

## Commands

```bash
wintune boot analyze
wintune boot analyze trace.etl
wintune boot trace --duration 60000
wintune startup --measured
```

### `wintune boot analyze`

Reads the latest boot metrics from
`Microsoft-Windows-Diagnostics-Performance/Operational`:

- Total boot duration and main-path breakdown (kernel, drivers, post-boot)
- Boot degradation flag and summary
- Slow startup components (services, drivers, applications)
- Disk-heavy startup hints when reported by Windows

Optional second argument: path to a login `.etl` from `boot trace` (adds event
count metadata).

```bash
wintune boot analyze --json
wintune boot analyze --json --output boot.json
```

### `wintune boot trace`

Starts a **live ETW login trace** for the current session (not the next reboot).
Saves an `.etl` file under `%LOCALAPPDATA%\WinTune\traces\`.

```bash
wintune boot trace --duration 30000   # 30 seconds
```

**Note:** ETW trace sessions often require an **elevated** shell. Full boot
tracing across reboot is a future enhancement; Phase 10 focuses on post-boot
analysis of Windows-recorded boot data plus optional login traces.

### `wintune startup --measured`

Lists startup entries and correlates them with measured boot/login component
delays when names match. Impact labels upgrade from heuristics to measured
values when data is available.

## Integration with scan / doctor / report

`wintune scan`, `wintune doctor`, and `wintune report` include a **boot**
section when data is available (JSON key `"boot"`). Recommendations may include:

| ID | Meaning |
| --- | --- |
| `WT-BOOT-001` | Last boot slower than 60 s |
| `WT-BOOT-002` | Windows reported boot degradation |
| `WT-STARTUP-001` | Measured startup component delay ≥ 3 s |

## Admin requirements

Reading the Diagnostic-Performance log or starting ETW sessions may require
**administrator privileges** on some machines. WinTune fails gracefully:

- `scan` / `doctor` continue with `"boot": { "available": false }`
- `boot analyze` prints the standard admin-required message

Over SSH, use an elevated session or a future WinTune Service (Phase 11).

## Data sources

| Source | API | Used for |
| --- | --- | --- |
| Diagnostic-Performance event log | `wevtapi` (`EvtQuery`) | `boot analyze`, scan boot section |
| Live login trace | `evntrace.h` (`StartTraceW`) | `boot trace` |
| Trace file stats | `ProcessTrace` | optional `.etl` supplement |

## Safety

- Read-only analysis; no system changes.
- No raw event log dumps in normal output.
- No disabling of startup items automatically.

See also [`roadmap.md`](roadmap.md) Phase 10 and [`metrics.md`](metrics.md).
