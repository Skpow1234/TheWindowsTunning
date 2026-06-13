# WinTune Windows Service (Phase 11)

Phase 11 adds an **optional, clearly removable** Windows Service that runs
WinTune locally with elevated privileges. The CLI talks to it over a **local
named pipe** so non-elevated shells (including SSH) can run privileged scans
and safe apply actions.

WinTune is **not** hidden persistence. Install and uninstall are explicit
commands with documented paths and identity.

## Service identity

| Property | Value |
| --- | --- |
| Service name | `WinTune` |
| Display name | WinTune Performance Agent |
| Binary | Same `wintune.exe` with `service run` |
| Start type | **Demand** (manual start after install) |
| Account | Local System (default SCM account) |
| IPC pipe | `\\.\pipe\WinTune` |
| Data directory | `%ProgramData%\WinTune\` |
| Cached scan | `%ProgramData%\WinTune\last_scan.json` |

Pipe ACL: **SYSTEM** and **Built-in Administrators** only. Remote pipe
connections are rejected.

## Commands

```bash
wintune service status
wintune service install      # admin required
wintune service uninstall    # admin required
wintune service start
wintune service stop
```

### Install workflow (admin PowerShell)

```powershell
.\build\Release\wintune.exe service install
.\build\Release\wintune.exe service start
.\build\Release\wintune.exe service status
```

### Uninstall

```powershell
wintune service stop
wintune service uninstall
```

## CLI integration: `--via-service`

```bash
wintune scan --json --via-service
wintune doctor --json --via-service
wintune apply WT-POWER-001 --via-service --yes
```

| Command | `--via-service` behavior |
| --- | --- |
| `scan` / `doctor` | Uses service when **`--json`** is set (returns full scan JSON from the elevated agent) |
| `apply` | Routes to service when flag is set **or** when CLI is not elevated and the service is reachable |
| Other commands | Local execution (for now) |

Text-mode `scan`/`doctor` with `--via-service` falls back to a local scan and
prints a note — use `--json --via-service` for the service path.

## IPC protocol

Length-prefixed UTF-8 JSON frames (`WIP1` magic + 32-bit length).

Example requests:

```json
{"cmd":"ping"}
{"cmd":"status"}
{"cmd":"scan","interval_ms":500}
{"cmd":"doctor"}
{"cmd":"apply","id":"WT-POWER-001","yes":1}
```

The service runs the same core functions as the CLI (`wt_run_scan`,
`wt_apply_recommendation`, etc.) inside the elevated process.

## Periodic health scans

While running, the service:

1. Runs an initial scan and writes `last_scan.json`
2. Repeats every **15 minutes** while active

Use `service status` to see whether a cached scan file exists.

## SSH usage

Non-elevated SSH sessions can use the service for privileged work **if** an
administrator has installed and started the service on the host:

```bash
ssh user@host "wintune scan --json --via-service"
ssh user@host "wintune apply WT-POWER-001 --yes"
```

The second command auto-routes to the service when the remote CLI is not
elevated and the pipe is reachable.

See [`ssh.md`](ssh.md).

## Safety

- Service must be **explicitly installed**; WinTune never self-installs.
- Uninstall removes the SCM registration.
- No network listeners; local pipe only.
- Apply actions still respect confirmation/`--yes` and the existing denylist.
- Dangerous actions remain blocked in the apply layer.

## Limitations (Phase 11)

- Single local pipe; no multi-user remote RPC.
- Service runs as Local System — document before install in regulated environments.
- No automatic start at boot (demand start); start manually or via policy.
- Full apply routing for `services restart` / HKLM startup via IPC is partial —
  power-plan apply is supported; extend in Phase 16.

See [`roadmap.md`](roadmap.md) Phase 11 and Phase 16.
