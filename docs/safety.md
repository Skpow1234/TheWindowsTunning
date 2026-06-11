# WinTune Safety Model

> WinTune does not blindly disable services, delete registry keys, disable
> security tools, or promise magical performance gains.

WinTune is a serious, measurable, safe Windows-native performance tool. It is
**not** a fake "PC cleaner", registry-tweaking booster, debloater, antivirus
replacement, or FPS optimizer. This document is the authoritative description
of what WinTune is and is not allowed to do.

---

## Core Safety Principles

1. **Measurement before recommendation.** Nothing is recommended without
   evidence collected over multiple samples.
2. **User approval before system changes.** Mutating commands require explicit
   confirmation unless `--yes` is passed.
3. **Dangerous actions are blocked even with `--yes`.**
4. **Rollback whenever practical.** Changes write rollback metadata.
5. **Official Windows APIs only.** No undocumented hacks, no UAC bypass.
6. **Local-first.** No cloud, no telemetry, no background upload.

---

## Read-Only by Default

These commands never modify the system and avoid requiring admin where possible:

```text
wintune scan
wintune top
wintune top --watch
wintune tui
wintune startup
wintune services
wintune power
wintune recommend
wintune report
wintune doctor
```

## Potentially Mutating (require confirmation)

```text
wintune apply
wintune power --set
wintune services restart
wintune startup disable
wintune startup enable
```

Mutating commands require confirmation unless `--yes` is explicitly passed.

---

## Allowed Safe Actions

- Show diagnostics and generate reports.
- Export JSON.
- Switch power plan using the official Power API (to an **existing** scheme).
- Restart a selected service after confirmation.
- Disable or delay a **user-approved** startup entry (per item).
- Recommend uninstalling software without uninstalling it automatically.
- Show update status and reboot requirement.
- Create rollback metadata.
- Show commands the user can run manually.

---

## Never (Hard Prohibitions)

WinTune must never behave like malware, scareware, adware, or a shady cleaner.

- Claim fake performance improvements.
- Delete user files automatically.
- Clean temporary files automatically (v1).
- Disable security tools, Microsoft Defender, Windows Update, firewall,
  UAC, BitLocker, or SmartScreen.
- Modify random registry keys; no registry cleaner.
- Install drivers silently; install unsigned drivers.
- Hide processes, inject into processes, hook system calls.
- Patch Windows binaries or modify kernel structures.
- Bypass UAC; evade antivirus.
- Collect telemetry or upload user data without consent.
- Run remote commands; change browser or DNS settings without consent.
- Touch credentials, cookies, tokens, or password stores.
- Clear event logs.
- Disable all startup apps or all services blindly.
- Delete System32 or WinSxS files; modify bootloader settings.

---

## Dangerous Actions Blocklist

These are **never** implemented as automatic recommendations and are blocked
even with `--yes`:

```text
Disable Microsoft Defender
Disable Windows Update
Disable firewall
Disable UAC
Disable BitLocker
Disable SmartScreen
Disable core Windows services
Delete System32 files
Delete WinSxS files manually
Clear event logs
Modify bootloader settings
Modify kernel settings
Install unsigned drivers
Patch system DLLs
Inject into system processes
Disable security-related scheduled tasks
Disable all startup apps blindly
Clean registry blindly
Run hidden background processes
Silently install persistence
Bypass admin requirements
```

### Protected Services (never auto-disabled)

Microsoft Defender, Windows Update, Firewall, UAC-related, networking,
storage, cryptographic, login/session, driver, BitLocker, and
SmartScreen/security services.

---

## Admin Privilege Handling

- Read-only commands should not require admin when possible.
- The app detects elevation via `wt_is_process_elevated()`.
- When admin is required, WinTune explains why and fails gracefully:

```text
This action requires administrator privileges.
Run PowerShell as Administrator and try again.
```

- No UAC bypass. No automatic self-elevation in v1.
- Over SSH, do not rely on GUI elevation prompts. Future privileged remote
  workflows use a local WinTune Windows Service + secure local IPC.

---

## Privacy

Default mode is local-only. No cloud, no telemetry, no account, no background
upload, no network calls (until an opt-in update check is added later).

WinTune does **not** collect: files, documents, browser history, credentials,
environment variables (by default), full command lines (by default), Wi-Fi
passwords, browser cookies, or process tokens.

Reports are stored locally only.

---

## Rollback

Mutating actions write rollback metadata where practical:

- Machine-wide: `%ProgramData%\WinTune\rollback\`
- User-only: `%LOCALAPPDATA%\WinTune\rollback\`

Each record captures the action, previous value, new value, and the command to
undo it. See `DESIGN.md` for the record schema.

---

## UX Tone

Calm, factual, and useful. No fear-based language, no fake urgency.

Avoid: `Your PC has 527 problems.`

Prefer:

```text
Disk active time was above 90% for 8 of 10 samples.
The top disk I/O process was MsMpEng.exe.
This can happen during antivirus scans.
Recommendation: wait for the scan to finish or schedule scans outside work hours.
```
