# WinTune over SSH

WinTune is designed to be SSH-friendly. It does **not** ship its own SSH
server — it works through the standard **Windows OpenSSH Server** by being a
well-behaved terminal program.

---

## Expected Remote Usage

```bash
ssh user@windows-machine "wintune scan"
ssh user@windows-machine "wintune scan --json"
ssh user@windows-machine "wintune doctor"
ssh user@windows-machine "wintune recommend --json"
ssh user@windows-machine "wintune report --format json --output report.json"
ssh user@windows-machine "wintune top --watch"          # falls back to snapshot
ssh -t user@windows-machine "wintune tui --safe-terminal"
```

The CLI and TUI avoid assumptions that only hold in a local Windows Terminal
session.

---

## Session Detection

WinTune detects how it is running:

| Signal | Meaning |
| ------ | ------- |
| stdout not a console | Piped/redirected output (typical `ssh host "cmd"`) |
| stdin not a console | Non-interactive (no prompts) |
| `SSH_CONNECTION` / `SSH_CLIENT` | Remote OpenSSH session |

Helpers:

- `wt_session_is_interactive()` — both stdin and stdout are consoles (full TUI,
  `--watch`, confirmation prompts).
- `wt_session_is_remote()` — SSH environment detected.

JSON documents include a `session` object:

```json
"session": {
  "interactive": false,
  "remote": true,
  "elevated": false
}
```

This helps automation decide whether mutating commands can prompt or need
`--yes`.

---

## SSH-Friendly Behavior

- **No GUI prompts** — mutating commands confirm on stdin or require `--yes`.
- **No UAC bypass** — elevation must come from the shell/session.
- **No mouse** — TUI is keyboard-only.
- **Piped stdout** — ANSI colors disabled automatically; no escape sequences on
  stdout unless interactive.
- **`--json`** — stdout is JSON only; incidental hints go to stderr and are
  suppressed in JSON mode where possible.
- **`--safe-terminal`** — ASCII bars, no color; recommended for SSH TUI.
- **Remote PTY sessions** — when SSH is detected and the session is interactive,
  WinTune auto-enables conservative rendering (same as `--safe-terminal`).

---

## Non-Interactive Commands

When stdin/stdout are not both interactive (common for `ssh host "wintune …"`):

- `top --watch` falls back to a single snapshot (silent in `--json` mode).
- `tui` is refused with hints for `scan --json` or `ssh -t … tui --safe-terminal`.
- Mutating commands refuse unless `--yes` is passed.

Example:

```bash
ssh user@host "wintune apply WT-POWER-001 --yes"
```

---

## Admin / Elevation over SSH

Mutating commands that need admin print a standard message:

```text
This action requires administrator privileges.
Over SSH, run from an elevated PowerShell or CMD session.
```

WinTune never attempts UAC bypass or credential tricks. Future privileged
remote workflows are intended to use a local **WinTune Windows Service** and
secure IPC, not unsafe elevation hacks.

---

## JSON for Remote Automation

JSON mode is the recommended interface for scripted remote diagnostics:

```bash
ssh user@host "wintune scan --json"   > host-scan.json
ssh user@host "wintune doctor --json" > host-doctor.json
```

Rules:

- **stdout** — JSON only (no banners, colors, or progress text).
- **stderr** — errors and optional human hints (suppressed for non-fatal notes
  in JSON mode).
- **Partial failures** — encoded inside JSON (`"available": false` per section).

Always flush explicitly at process exit so piped SSH sessions never drop output.

---

## Validation Checklist

```bash
ssh user@host "wintune scan"
ssh user@host "wintune scan --json"
ssh user@host "wintune top --watch"
ssh user@host "wintune top --watch --json"
ssh -t user@host "wintune tui --safe-terminal"
```

Expected:

1. Text scan over SSH prints plain line-oriented output.
2. JSON scan is valid JSON with a `session` block.
3. `top --watch` over one-shot SSH prints one table (no hang).
4. TUI over `ssh -t` works with ASCII/safe rendering.
