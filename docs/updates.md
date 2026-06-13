# Windows Update & Reboot Readiness (Phase 13)

Phase 13 explains **update and reboot state** without becoming an installer.
WinTune never installs updates, disables Windows Update, or changes update
settings.

## Command

```bash
wintune updates
wintune updates --json
```

## What is reported

| Area | Source |
| --- | --- |
| Reboot pending | Registry (WU, CBS, pending file renames) |
| WU service state | Service Control Manager (`wuauserv`) |
| Last successful check | Windows Update registry results |
| Last install time | Windows Update registry results |
| Last install title | Windows Update history (COM) |
| Pending updates | Windows Update Agent search (COM) |

The full `wintune updates` command runs a WU search (`IsInstalled=0`) and may
take several seconds. **`scan`** and **`doctor`** use a fast registry-only
pass; **`recommend`** and **`doctor`** also run the pending-update search so
`WT-UPDATE-*` recommendations can include mandatory/pending counts.

## Example text output

```text
Windows Update

  Reboot required:  yes
    - Windows Update pending reboot
  WU service:       running
  Last check:       2026-06-11T08:12:00Z
  Last install:     2026-06-10T22:01:00Z  (Security intelligence update)

Pending updates:  3 (1 mandatory)
```

## Recommendations

| Id | When |
| --- | --- |
| `WT-UPDATE-001` | Reboot pending (WU, CBS, or file renames) |
| `WT-UPDATE-002` | Mandatory updates pending |
| `WT-UPDATE-003` | Many pending updates (5+) |
| `WT-UPDATE-004` | Windows Update service stopped |

When reboot is pending and blockers are detected, `WT-BLOCKER-001` may also
appear (`wintune blockers`).

Shown in `wintune recommend`, `wintune doctor`, and scan JSON when applicable.

## JSON

```bash
wintune updates --json
wintune scan --json        # includes updates.reboot_required (fast path)
```

Scan JSON includes an `updates` object with reboot flags and timestamps; the
pending list is populated when a full search has run (`recommend`/`doctor` or
`wintune updates`).

## Safety

- **Read-only** — no installs, no service changes, no registry writes.
- Never disables Windows Update or bypasses update policy.
- Does not download or upload update metadata to the network beyond what the
  local Windows Update Agent already does when searching.

## Admin notes

Some machines return partial WU history or search results without elevation.
Reboot-pending registry flags remain useful even when the COM search fails.

## SSH / automation

```bash
ssh user@host "wintune updates --json"
ssh user@host "wintune recommend --json"   # includes WT-UPDATE-* when applicable
```

See [`ssh.md`](ssh.md).
