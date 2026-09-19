# Boot and Login Analysis (Phases 10 & 31)

WinTune reads **Windows Diagnostic-Performance** data — the same ETW-backed
event log Windows uses for boot and login performance monitoring. Output is
**summarized** for humans and JSON; WinTune never dumps raw ETW events to the
terminal.

Phase 31 adds an optional **reboot-spanning Autologger** so you can measure the
*next* boot, not only post-boot event history.

## Commands

```bash
wintune boot analyze
wintune boot analyze trace.etl
wintune boot trace --duration 60000
wintune boot arm
wintune boot status
wintune boot disarm
wintune startup --measured
```

### `wintune boot analyze`

Reads the latest boot metrics from
`Microsoft-Windows-Diagnostics-Performance/Operational`:

- Total boot duration and main-path breakdown (kernel, drivers, post-boot)
- Boot degradation flag and summary
- Slow startup components (services, drivers, applications)
- Disk-heavy startup hints when reported by Windows

Optional second argument: path to a login `.etl` from `boot trace`.

When no path is given and a Phase 31 reboot ETL is ready under
`%ProgramData%\WinTune\traces\`, analyze auto-includes it (and stops the
Autologger session so it does not keep writing).

```bash
wintune boot analyze --json
wintune boot analyze --json --output boot.json
```

`scan` / `doctor` / `report` use the same collector, so a ready reboot ETL is
picked up there too.

### `wintune boot arm` (Phase 31)

Configures a Windows **Autologger** session (`WinTuneBoot`) that starts at the
**next reboot**. Requires an elevated shell (writes HKLM Autologger keys).

```bash
wintune boot arm
wintune boot arm --json
```

Trace file (after reboot):

```text
%ProgramData%\WinTune\traces\boot-next.etl
```

**Workflow:**

1. `wintune boot arm` (admin)
2. Reboot
3. After login: `wintune boot analyze` (or `doctor` / `scan`)
4. `wintune boot disarm` when finished (keeps the `.etl`)

### `wintune boot status`

Shows arm state without changing anything:

| State | Meaning |
| --- | --- |
| `idle` | Not armed |
| `pending_reboot` | Autologger configured; reboot not yet done |
| `capturing` | Reboot done; session may still be writing |
| `ready` | Reboot ETL present; safe to analyze |

```bash
wintune boot status
wintune boot status --json
```

### `wintune boot disarm`

Stops the Autologger session (if running), removes the HKLM Autologger keys,
and deletes the arm state file. Keeps any existing reboot `.etl` for later
`boot analyze <path>`.

### `wintune boot trace`

Starts a **live ETW login trace** for the current session (not the next reboot).
Saves an `.etl` file under `%LOCALAPPDATA%\WinTune\traces\`.

```bash
wintune boot trace --duration 30000   # 30 seconds
```

**Note:** ETW trace sessions often require an **elevated** shell. For full
next-boot capture use `boot arm` instead.

### `wintune startup --measured`

Lists startup entries and correlates them with measured boot/login component
delays when names match. Impact labels upgrade from heuristics to measured
values when data is available.

## Cold vs warm profiles (Phase 32)

`boot analyze` reads up to **8** recent Event 100 samples and classifies each:

| Kind | Heuristic |
| --- | --- |
| `cold` | `BootKernelInitTime` > 300 ms, or reboot-after-install |
| `warm` | `BootKernelInitTime` ≤ 300 ms (typical Fast Startup / hybrid) |
| `unknown` | Kernel init field missing |

History summary includes average duration, cold/warm counts and averages, and
how many boots were slow (≥ 60 s) or degraded.

**Recommendations:** `WT-BOOT-001` fires only when slow boots form a **pattern**
(at least two slow samples, or a multi-boot average ≥ 60 s). A single slow boot
is shown in history but does not alarm.

## Driver / service waterfall (Phase 33)

For the latest boot, WinTune collects degradation events **101–110** and builds
an **impact-ordered waterfall** (longest attributed delay first):

| Event | Typical kind |
| --- | --- |
| 101 | application |
| 102 / 109 | driver / device |
| 103 | service |
| 104–110 | other boot path delays |

Each row may include start offset (ms after `BootStartTime`) and a matched
Win32 service name/state/PID when the SCM list matches. See
`wintune boot analyze` and JSON `boot.components` / `waterfall_total_ms`.

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
| `wintune boot arm` / `disarm` | Exit non-zero; HKLM Autologger requires elevation |
| `wintune boot status` | Read-only; works without admin when possible |
| `wintune startup --measured` | Falls back to heuristic impact; stderr note if measured data unavailable |

**Run from an elevated PowerShell or CMD** to get full boot metrics:

```powershell
.\build\Release\wintune.exe boot arm
# reboot, then:
.\build\Release\wintune.exe boot analyze
.\build\Release\wintune.exe boot disarm
```

Over SSH, use an elevated session (`Run as administrator` on the remote shell),
the WinTune Service (`wintune scan --json --via-service`), or a future WinTune
Service-based workflow. See [`service.md`](service.md).

## Data sources

| Source | API | Used for |
| --- | --- | --- |
| Diagnostic-Performance event log | `wevtapi` (`EvtQuery`) | `boot analyze`, scan boot section |
| Live login trace | `evntrace.h` (`StartTraceW`) | `boot trace` |
| Reboot Autologger | HKLM WMI Autologger + `ControlTraceW` | `boot arm` / `status` / `disarm` |
| Trace file stats | `ProcessTrace` | optional `.etl` supplement (event count) |

## Safety

- Analysis is read-only aside from optional Autologger arm/disarm.
- Autologger only enables Diagnostic-Performance and Kernel-Process providers.
- No raw event log dumps in normal output.
- No disabling of startup items automatically.
- Always `boot disarm` when finished so the session does not linger across boots.

See also [`roadmap.md`](roadmap.md) Phases 10 and 31 and [`metrics.md`](metrics.md).
