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
| Default start type | **Manual (demand)** — use `--auto-start` for boot start |
| Default account | **Local System** — choose at install time (see below) |
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

### Install options (admin)

```powershell
# Default: Local System, manual start
wintune service install

# Start automatically with Windows
wintune service install --auto-start

# Choose the service account (four supported options)
wintune service install --account system          # Local System (default)
wintune service install --account localservice    # NT AUTHORITY\LocalService
wintune service install --account virtual         # NT SERVICE\WinTune (VSA)
wintune service install --account "DOMAIN\User" --account-password "<secret>"
```

| `--account` value | Runs as | Notes |
| --- | --- | --- |
| `system` | Local System | Highest privilege; default |
| `localservice` | NT AUTHORITY\LocalService | Lower privilege network service account |
| `virtual` | NT SERVICE\WinTune | Virtual service account (VSA) for the WinTune service |
| `DOMAIN\User` | Custom | Requires `--account-password`; use for least-privilege deployments |

Combine with `--auto-start` when you want the agent to start at boot.

`wintune service status` shows the installed start type, account name, and
whether the pipe is reachable.

### Install workflow (admin PowerShell)

```powershell
.\build\Release\wintune.exe service install --account localservice
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
wintune scan --via-service
wintune scan --json --via-service
wintune doctor --via-service
wintune apply WT-POWER-001 --via-service --yes
wintune power --set performance --via-service --yes
wintune services restart Spooler --via-service --yes
wintune startup disable "HKCU\...\Run\SomeApp" --via-service --yes
```

| Command | Service routing |
| --- | --- |
| `scan` / `doctor` | Uses service when `--via-service` is set (text or JSON) |
| `apply` | Routes when `--via-service` is set **or** CLI is not elevated and the service is reachable |
| `power --set` | Same auto-route as `apply` |
| `services restart` | Same auto-route as `apply` |
| `startup enable/disable` | Same auto-route as `apply` |
| Other commands | Local execution |

When the CLI is not elevated and the WinTune service is running, mutating
commands automatically use the service without requiring `--via-service`.

## IPC protocol

Length-prefixed UTF-8 JSON frames (`WIP1` magic + 32-bit length).

Example requests:

```json
{"cmd":"ping"}
{"cmd":"status"}
{"cmd":"scan","interval_ms":500}
{"cmd":"scan","format":"text"}
{"cmd":"doctor","format":"text"}
{"cmd":"apply","id":"WT-POWER-001","yes":1}
{"cmd":"power_set","plan":"performance","yes":1}
{"cmd":"restart_service","name":"Spooler","yes":1}
{"cmd":"startup_set","id":"HKCU\\...\\Run\\App","enable":0,"yes":1}
```

Text responses for `scan`/`doctor` use the same human-readable format as the
local CLI (`format":"text"` in the request).

The service runs the same core functions as the CLI (`wt_run_scan`,
`wt_apply_recommendation`, safe actions, etc.) inside the elevated process.

## Periodic health scans

While running, the service:

1. Runs an initial scan and writes `last_scan.json`
2. Repeats every **15 minutes** while active

Use `service status` to see whether a cached scan file exists.

## SSH usage

Non-elevated SSH sessions can use the service for privileged work **if** an
administrator has installed and started the service on the host:

```bash
ssh user@host "wintune scan --via-service"
ssh user@host "wintune doctor --via-service"
ssh user@host "wintune apply WT-POWER-001 --yes"
```

Mutating commands auto-route to the service when the remote CLI is not
elevated and the pipe is reachable.

See [`ssh.md`](ssh.md).

## Safety

- Service must be **explicitly installed**; WinTune never self-installs.
- Uninstall removes the SCM registration.
- No network listeners; local pipe only.
- Apply actions still respect confirmation/`--yes` and the existing denylist.
- Dangerous actions remain blocked in the apply layer.
- Choose the least-privileged account that meets your deployment needs.

## Limitations

- Single local pipe; no multi-user remote RPC.
- Custom account password is passed on the install command line — prefer
  interactive admin sessions or policy-based deployment in regulated environments.
- Re-install is required to change account or start type (uninstall first).

See [`roadmap.md`](roadmap.md) Phase 11 and Phase 16.
