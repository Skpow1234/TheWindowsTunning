# Scheduled Tasks (Phase 12)

Phase 12 completes the startup impact picture with **Task Scheduler COM**
inspection, logon/boot trigger detection, safe disable/delay for user-approved
third-party tasks, and measured impact when boot analysis data is available.

WinTune never disables Microsoft or security-related tasks automatically.

## Commands

```bash
wintune tasks list
wintune tasks list --logon
wintune tasks list --measured
wintune tasks list --json

wintune tasks disable "<id>"
wintune tasks enable "<id>"
wintune tasks delay "<id>" --seconds 30
```

### Startup integration

```bash
wintune startup --include-tasks
wintune startup --include-tasks --measured
wintune startup delay "<id>" --seconds 30
```

`startup --include-tasks` shows logon/boot scheduled tasks below the registry
and Startup-folder entries. Use `wintune tasks list` for the full table.

## Task ids

Ids are stable strings of the form:

```text
task:\Vendor\AppName
```

Example:

```text
task:\Adobe Acrobat Update Task
```

List ids with `wintune tasks list` or `wintune tasks list --json`.

## Filters

| Flag | Behavior |
| --- | --- |
| (default) | Boot + logon triggers (startup-heavy) |
| `--logon` | Logon triggers only |
| `--measured` | Correlate with Phase 10 boot event data when available |

## Safe actions

| Action | Effect |
| --- | --- |
| `tasks disable` | Sets task `Enabled = false` via Task Scheduler |
| `tasks enable` | Re-enables a disabled task |
| `tasks delay` | Sets logon-trigger delay (ISO 8601 `PTxxS`) |
| `startup delay` | Writes Task Manager–compatible delayed-start flag in `StartupApproved` |

All mutating commands:

- Require confirmation unless `--yes` is passed
- Write rollback records (`wintune rollback list`)
- Refuse protected Microsoft/security tasks
- Auto-route through the WinTune service when not elevated (same as `apply`)

## Protected tasks

WinTune blocks changes to:

- Tasks under `\Microsoft\`
- Tasks whose author/path matches security/update/defender patterns
- See [`safety.md`](safety.md)

## JSON output

```bash
wintune tasks list --json
```

Fields include `id`, `name`, `path`, `command`, `trigger`, `delay_seconds`,
`impact`, `protected`, and optional `measured_ms`.

## Service IPC

When the WinTune service is running, task and startup-delay actions can be
routed over the local pipe (automatic when CLI is not elevated):

```json
{"cmd":"task_set","id":"task:\\Vendor\\App","enable":0,"yes":1}
{"cmd":"task_set","id":"task:\\Vendor\\App","delay_seconds":30,"yes":1}
{"cmd":"startup_delay","id":"HKCU\\Run\\App","delay_seconds":30,"yes":1}
```

See [`service.md`](service.md).

## Limitations

- Task enumeration can take a few seconds on busy systems.
- Delay applies to **logon** triggers only (`tasks delay`).
- Registry `RunOnce` entries do not support delayed start via `StartupApproved`.
- Re-registering tasks may require the same privileges as Task Scheduler UI.

See [`roadmap.md`](roadmap.md) Phase 12.
