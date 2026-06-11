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
ssh user@windows-machine "wintune top --watch"
ssh user@windows-machine "wintune tui --safe-terminal"
```

The CLI and TUI must avoid assumptions that only hold in a local Windows
Terminal session.

---

## SSH-Friendly Requirements

- Do not require GUI prompts.
- Do not rely on UAC popups.
- Do not require mouse input.
- Do not require Windows Terminal-specific features.
- Provide an ASCII fallback (`--no-unicode`).
- Provide a no-color mode (`--no-color`).
- Provide a JSON mode (`--json`).
- Detect non-interactive sessions and render conservatively.
- Avoid writing binary/control output unless in TUI mode.
- Mutating commands must clearly explain when admin is required.

---

## Non-Interactive Detection

When standard input/output is not an interactive terminal (the common case for
`ssh host "wintune ..."`), WinTune:

- Disables animations and cursor manipulation.
- Avoids the alternate screen buffer.
- Falls back to plain, line-oriented output.
- Refuses to start the full TUI and suggests `scan` / `top` / `doctor` instead.

`--safe-terminal` forces this conservative behavior explicitly, which is the
recommended way to run the dashboard remotely:

```bash
ssh user@host "wintune tui --safe-terminal"
```

---

## Admin / Elevation over SSH

Over SSH, mutating commands may fail if the session is not elevated. WinTune
never attempts a UAC bypass. Instead it explains the situation clearly:

```text
This action requires administrator privileges.
Run from an elevated shell or use a future WinTune Service-based workflow.
```

- Never attempt UAC bypass.
- Never attempt credential theft, token abuse, or privilege escalation tricks.
- Do not rely on GUI elevation prompts in an SSH session.

Future privileged remote workflows are intended to use a local **WinTune
Windows Service** plus a secure local IPC mechanism (named pipe), not unsafe
privilege tricks.

---

## JSON for Remote Automation

JSON mode is the recommended interface for fleet-style or scripted remote
diagnostics:

```bash
ssh user@host "wintune scan --json"   > host-report.json
ssh user@host "wintune recommend --json"
```

In JSON mode WinTune emits **only** JSON — no banners, progress text, or ANSI
colors — so output can be piped and parsed safely. Partial failures are encoded
inside the JSON payload rather than printed separately.

---

## Phase 9: SSH Hardening

Later work focuses on:

- Better non-interactive detection.
- Better safe-terminal rendering.
- Better JSON remote behavior.
- Clear admin-required messages over SSH.

Validation commands:

```bash
ssh user@host "wintune scan"
ssh user@host "wintune scan --json"
ssh user@host "wintune top --watch"
```
