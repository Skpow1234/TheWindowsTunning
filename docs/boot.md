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

On some Windows setups, reading the **Diagnostic-Performance** event log returns
**access denied** unless the shell is elevated. That is expected — the log is
protected on certain builds and policies. WinTune does not bypass this; it fails
safely and explains what to do.

| Command | When access is denied |
| --- | --- |
| `wintune scan` / `wintune doctor` / `wintune report` | Continue normally; boot section omitted (`"boot": { "available": false }` in JSON) |
| `wintune boot analyze` | Exit non-zero; prints the standard admin-required message |
| `wintune boot trace` | Exit non-zero; ETW session start requires elevation on most systems |
| `wintune startup --measured` | Falls back to heuristic impact; stderr note if measured data unavailable |

**Run from an elevated PowerShell or CMD** to get full boot metrics:

```powershell
# From the repo after building:
.\build\Release\wintune.exe boot analyze

# Or if wintune is on PATH:
wintune boot analyze
wintune boot analyze --json
```

Over SSH, use an elevated session (`Run as administrator` on the remote shell)
or a future WinTune Service (Phase 11). See [`ssh.md`](ssh.md) for remote
admin guidance.

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
