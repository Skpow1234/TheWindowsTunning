# Apply and rollback

WinTune separates **diagnostics** (`scan`, `recommend`, `doctor`) from **mutating
actions** (`apply`, `power --set`, `startup disable`, etc.). Every automatic
change is explainable, confirmed by default, and recorded for rollback when
practical.

## Central action map

Recommendation IDs are mapped to handlers in `src/actions/action_map.c` — a
single source of truth for what `wintune apply` can execute.

| Recommendation ID | Kind | Notes |
|-------------------|------|-------|
| `WT-POWER-001` | Switch to High performance | Rollback restores previous GUID |
| `WT-POWER-002` | Switch to Balanced | Rollback restores previous GUID |
| `WT-STARTUP-DISABLE` | Disable startup entry | Requires target startup id |
| `WT-STARTUP-DELAY` | Delay startup entry | Requires target id; default 30 s |
| `WT-TASK-DISABLE` | Disable logon/boot task | Requires task id |
| `WT-TASK-DELAY` | Delay logon task | Requires task id; default 30 s |

Advisory recommendations (`WT-MEMORY-001`, `WT-DISK-*`, `WT-CPU-001`,
`WT-BOOT-*`, `WT-STARTUP-001`, `WT-UPDATE-*`, `WT-BLOCKER-001`, …) explain
what was measured but have **no automatic apply**. `wintune apply` returns a
clear message pointing you to the suggested command in `wintune recommend`.

Scan-time recommendations `WT-STARTUP-002` and `WT-TASK-001` include a full
`wintune apply …` command with the target id when a high-impact entry is found.

## Examples

```powershell
# Power (no extra arguments)
wintune apply WT-POWER-001
wintune apply WT-POWER-001 --yes

# Startup / tasks (target id as second argument)
wintune apply WT-STARTUP-DISABLE "HKCU\Run:OneDrive"
wintune apply WT-STARTUP-DELAY "HKCU\Run:Docker Desktop" --seconds 45
wintune apply WT-TASK-DELAY "task:\Vendor\App" --seconds 30

# Over SSH (non-interactive — use --yes)
ssh user@host "wintune apply WT-POWER-001 --yes"
```

List startup and task ids with `wintune startup` and `wintune startup --include-tasks`.

## Service-based apply

When the WinTune Windows Service is installed and running, add `--via-service`
so privileged changes run inside the elevated service process:

```powershell
wintune apply WT-POWER-001 --via-service --yes
wintune apply WT-STARTUP-DISABLE "HKLM\Run:App" --via-service --yes
```

If the pipe is unreachable, WinTune fails with a clear error instead of
attempting UAC bypass.

## Rollback

Successful mutating actions write JSON records under:

- `%ProgramData%\WinTune\rollback\` (machine-wide / admin actions)
- `%LOCALAPPDATA%\WinTune\rollback\` (per-user actions)

```powershell
wintune rollback list
wintune rollback list --json
wintune rollback apply 20260611-143022-123
wintune rollback apply 20260611-143022-123 --yes
```

Each record includes `action_id` (recommendation or target id), `action_type`,
`previous_value`, `new_value`, and a suggested `rollback_command`.

Supported rollback types:

| `action_type` | Undo behavior |
|---------------|---------------|
| `power_plan_change` | Restore previous power scheme GUID |
| `startup_approved` | Restore previous StartupApproved flag |
| `startup_delay` | Restore previous delay / enabled state |
| `task_enabled` | Re-enable or disable task |
| `task_delay` | Restore previous logon delay |

Service restarts are intentionally **not** rollback-recorded (restart is not
reversible to a prior runtime state).

## Safety

- Dangerous actions (disable Defender, firewall, etc.) are never in the action map.
- Microsoft / security startup entries and protected scheduled tasks are refused.
- Even `--yes` cannot override these blocks.
