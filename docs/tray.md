# WinTune system tray (Phase 19)

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
| **Run doctor…** | Opens a console and runs `wintune doctor` |
| **Run report…** | Runs `wintune report`, saves under Documents, opens the file |
| **Open last report** | Opens the newest report in Documents, or the service cache JSON |
| **Open reports folder** | Opens `%USERPROFILE%\Documents\WinTune\Reports` |
| **Open CLI menu…** | Launches `Launch-WinTune.cmd` (interactive CLI) |
| **Live dashboard (TUI)…** | Opens a console with `wintune tui` |
| **About WinTune** | Version and safety notice |
| **Exit** | Removes the tray icon and stops the process |

## Status window

The status window shows:

- Whether the **WinTune Windows Service** is reachable (named pipe `\\.\pipe\WinTune`)
- CPU and memory from `%ProgramData%\WinTune\last_scan.json` when the service has
  cached a scan
- Host and OS lines parsed from that cache

**Refresh** re-reads the cache file. It does not mutate the system.

If the service is not installed, status shows “CLI-only mode” and doctor/report
still work by spawning the CLI directly.

## Service integration

When the Phase 11 service is installed and running:

- The tray detects it with a `ping` IPC call.
- Cached scan data comes from `%ProgramData%\WinTune\last_scan.json` (written by
  the service on periodic scans).

The tray does **not** require the service. All menu actions fall back to spawning
`wintune.exe` next to the running binary.

## Architecture

```text
wintune tray
  ├── Win32 message loop + Shell_NotifyIconW
  ├── tray_status.c   read-only status window
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
