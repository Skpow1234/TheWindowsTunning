# WinTune TUI

The TUI is an optional, interactive live dashboard. It is **second** to the
CLI: every feature remains usable without it, and automation never requires it.

WinTune renders the TUI with **manual ANSI/VT escape sequences** — no curses,
ncurses, PDCurses, notcurses, FTXUI, or other terminal frameworks in v1.

---

## Launching

```bash
wintune tui
wintune tui --interval 1000
wintune tui --no-color
wintune tui --no-unicode
wintune tui --safe-terminal
```

The TUI must work in Windows Terminal, PowerShell, CMD (with VT enabled), the
VS Code terminal, and SSH sessions.

---

## Rendering Modes

| Mode             | Trigger                | Behavior                                  |
| ---------------- | ---------------------- | ----------------------------------------- |
| Unicode          | default                | Box-drawing glyphs, block bars            |
| ASCII fallback   | `--no-unicode`         | `+`, `-`, `|`, `#` characters             |
| No-color         | `--no-color`           | No ANSI color sequences                   |
| Safe terminal    | `--safe-terminal`      | Conservative output for SSH/unknown terms |
| Non-interactive  | auto-detected          | Refuses TUI; suggests `scan`/`top`        |

---

## Layout (Unicode)

```text
┌────────────────────────────── WinTune Live ──────────────────────────────┐
│ Host: DESKTOP-9KD2       Uptime: 3d 04h       Power: Balanced            │
├──────────────────────────────────────────────────────────────────────────┤
│ CPU  ███████████░░░░░░░░░░░░░░░░  38%                                    │
│ RAM  ██████████████████░░░░░░░░░  72%   23.1 GB / 32 GB                  │
│ DISK ███████████████████████░░░░  84%   C: 141 GB free                   │
│ NET  ↓ 12.4 MB/s   ↑ 1.1 MB/s                                           │
├──────────────────────────────────────────────────────────────────────────┤
│ Top Processes                                                            │
│ PID     Name                 CPU%     Memory       Disk                   │
│ 8420    chrome.exe           21.4     2.4 GB       8 MB/s                 │
│ 9921    docker.exe           14.0     1.7 GB       2 MB/s                 │
│ 5312    MsMpEng.exe          8.2      640 MB       51 MB/s                │
├──────────────────────────────────────────────────────────────────────────┤
│ Keys: q quit | r refresh | p power | s services | d disk | ? help         │
└──────────────────────────────────────────────────────────────────────────┘
```

## Layout (ASCII fallback)

```text
+---------------------------- WinTune Live ----------------------------+
| Host: DESKTOP-9KD2      Uptime: 3d 04h      Power: Balanced          |
+---------------------------------------------------------------------+
| CPU  [###########-------------------] 38%                            |
| RAM  [##################------------] 72%  23.1 GB / 32 GB           |
| DISK [#######################-------] 84%  C: 141 GB free            |
| NET  Down: 12.4 MB/s  Up: 1.1 MB/s                                  |
+---------------------------------------------------------------------+
| Top Processes                                                       |
| PID     Name                 CPU%     Memory       Disk              |
| 8420    chrome.exe           21.4     2.4 GB       8 MB/s            |
| 9921    docker.exe           14.0     1.7 GB       2 MB/s            |
| 5312    MsMpEng.exe          8.2      640 MB       51 MB/s           |
+---------------------------------------------------------------------+
| Keys: q quit | r refresh | p power | s services | d disk | ? help    |
+---------------------------------------------------------------------+
```

---

## Controls

```text
q     quit
r     refresh
p     power view
s     services view
d     disk view
m     memory view
n     network view
?     help
```

The TUI must **never** apply system changes without confirmation.

---

## VT Sequences Used

```c
#define WT_VT_CLEAR_SCREEN "\x1b[2J"
#define WT_VT_CURSOR_HOME  "\x1b[H"
#define WT_VT_HIDE_CURSOR  "\x1b[?25l"
#define WT_VT_SHOW_CURSOR  "\x1b[?25h"
#define WT_VT_ALT_SCREEN   "\x1b[?1049h"
#define WT_VT_MAIN_SCREEN  "\x1b[?1049l"
#define WT_VT_RESET        "\x1b[0m"
```

Console helpers (see `platform/console.h`):

```c
WT_Result wt_console_enable_vt(void);
WT_Result wt_console_get_size(int* rows, int* cols);
WT_Result wt_console_clear(void);
WT_Result wt_console_move_cursor(int row, int col);
WT_Result wt_console_hide_cursor(void);
WT_Result wt_console_show_cursor(void);
int       wt_console_is_interactive(void);
int       wt_console_supports_color(void);
```

---

## Terminal State Safety

- Always restore terminal state on exit.
- If the TUI crashes or exits for any reason, the cursor must become visible
  again and the main screen buffer must be restored.
- Default refresh interval is 1000 ms; TUI CPU overhead must stay low enough to
  avoid distorting the measurements it displays.
