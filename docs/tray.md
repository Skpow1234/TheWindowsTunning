# WinTune system tray (Phases 19 + 42)

WinTune includes an optional **native Win32 system tray** for users who prefer
a GUI shortcut without Electron, WebView, or a browser dashboard.

The tray app is **read-only by default**. It launches CLI commands in a console
for diagnostics. Mutating actions (`apply`, `power --set`, etc.) always require
explicit confirmation in the CLI — the tray never applies changes silently.

## Start the tray

From an extracted ZIP or install folder:

```powershell
wintune tray
```

Or double-click:

```text
Start-Tray.cmd
```

Only one tray instance runs at a time. A second launch shows a notice and exits.

The tray detaches from any console window (`FreeConsole`) so it can run quietly
in the background.

## Tray menu

| Action | Behavior |
|--------|----------|
| **Show status** | Read-only summary window (double-click the tray icon for the same) |
| **Quick scan…** | Short in-process scan into the status window (Phase 42 mini-doctor) |
| **Run doctor…** | Opens a console and runs `wintune doctor` |
| **Run report…** | Runs `wintune report`, saves under Documents, opens the file |
| **Open last report** | Opens the newest report in Documents, or the service cache JSON |
| **Open reports folder** | Opens `%USERPROFILE%\Documents\WinTune\Reports` |
| **Open CLI menu…** | Launches `Launch-WinTune.cmd` (interactive CLI) |
| **Live dashboard (TUI)…** | Opens a console with `wintune tui` |
| **Start with Windows** | Opt-in: adds/removes `HKCU\...\Run\WinTuneTray` (current user only; off by default) |
| **About WinTune** | Version and safety notice |
| **Exit** | Removes the tray icon and stops the process |

Hover tip shows a short live summary (`CPU` / `RAM` / power plan) and refreshes
about every 30 seconds. After a Quick scan with findings, the tip also shows a
recommendation count.

## Status window

The status window shows:

- Whether the **WinTune Windows Service** is reachable (named pipe `\\.\pipe\WinTune`)
- **Live** CPU and memory sample (short PDH + `GlobalMemoryStatusEx`)
- Power plan and AC/battery when available
- Host and OS identity
- Cached scan path when `%ProgramData%\WinTune\last_scan.json` exists
- **Mini-doctor** results after Quick scan (recommendations + CLI apply hint)

Buttons:

| Button | Behavior |
|--------|----------|
| **Quick scan** | 2 short local samples (~1–2 s); recommendations only; never applies |
| **Refresh** | Re-samples live metrics and re-reads the cache (keeps last Quick scan) |
| **Close** | Closes the status window |

If the service is not installed, status shows “CLI-only mode” and doctor/report
still work by spawning the CLI directly.

## Quick scan (Phase 42)

Quick scan is the tray mini-doctor:

1. Runs `wt_run_scan` in-process (2 samples, short intervals).
2. Builds recommendations with the same engine as `wintune doctor`.
3. Shows top findings and, when applicable, a CLI hint such as
   `wintune apply WT-POWER-001`.

It does **not**:

- Call `apply`, change power plans, restart services, or touch startup entries
- Bypass confirmation or elevation
- Run silently in the background without the user choosing Quick scan

For a full console doctor, use **Run doctor…** instead.

## Start with Windows

Off by default. When enabled, WinTune writes only:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
  WinTuneTray = "C:\path\to\wintune.exe" tray
```

No machine-wide Run key, no Task Scheduler persistence, and no silent install.
Toggle the menu item again to remove the value.

## Service integration

When the Phase 11 service is installed and running:

- The tray detects it with a `ping` IPC call.
- Cached scan data comes from `%ProgramData%\WinTune\last_scan.json` (written by
  the service on periodic scans).

The tray does **not** require the service. All menu actions fall back to spawning
`wintune.exe` next to the running binary (except Quick scan, which is in-process).

## Architecture

```text
wintune tray
  ├── Win32 message loop + Shell_NotifyIconW
  ├── tray_status.c   read-only status + Quick scan (scan + recommend)
  └── spawns wintune.exe for doctor / report / tui / CLI launcher
```

No separate Electron binary. No localhost web UI. Same `wintune.exe` as the CLI.

## SSH and automation

The tray is for **local interactive Windows sessions** only. For remote or
scripted use, continue with:

```bash
ssh user@host "wintune doctor"
ssh user@host "wintune scan --json"
```

## Related docs

- [`service.md`](service.md) — background agent and named pipe IPC
- [`cli.md`](cli.md) — full command reference
- [`architecture.md`](architecture.md) — module layout
