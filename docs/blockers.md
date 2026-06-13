# Restart Manager & Reboot Blockers (Phase 14)

Phase 14 helps explain **what may block restart or update completion** without
force-closing applications.

## Command

```bash
wintune blockers
wintune blockers --json
```

## What is reported

| Area | Source |
| --- | --- |
| Reboot pending context | Registry (same flags as `wintune updates`) |
| Shutdown-blocking apps | `ShutdownBlockReasonQuery` on visible top-level windows |
| Locked files | Restart Manager (`RmStartSession` / `RmGetList`) for pending file rename paths |
| Pending file operations | `PendingFileRenameOperations` registry value |

## Example text output

```text
Restart / update blockers

  Reboot pending:   yes
    - Windows Update pending reboot

Applications blocking shutdown

PID      Process                  Kind         Reason
8420     code.exe                 shutdown     Saving files...

Locked files (Restart Manager)

  C:\Program Files\Example\library.dll
    PID 4420  ExampleApp.exe
```

## Recommendations

| Id | When |
| --- | --- |
| `WT-BLOCKER-001` | Reboot pending **and** shutdown blockers or file locks detected |

Shown in `wintune recommend` and `wintune doctor` when applicable.

## JSON

```bash
wintune blockers --json
```

Top-level object includes a `blockers` section with `process_blockers` and
`locked_files` arrays.

## Safety

- **Read-only** — WinTune never terminates processes or closes windows.
- Close listed applications manually when you are ready.
- Even when no blockers are found, a reboot may still be required (`wintune updates`).

## Related commands

```bash
wintune updates          # reboot/update state
wintune updates --json
wintune recommend        # may include WT-BLOCKER-001
wintune doctor
```

## SSH / automation

```bash
ssh user@host "wintune blockers --json"
```

## Admin notes

Restart Manager may return partial results without elevation on some paths.
Shutdown block reasons are only visible for applications that registered a
block reason on a visible window.
