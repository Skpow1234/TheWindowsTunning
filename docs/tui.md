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
wintune tui --theme compact
wintune tui --theme mono
wintune tui --sort cpu
```

The TUI must work in Windows Terminal, PowerShell, CMD (with VT enabled), the
VS Code terminal, and SSH sessions.

**Minimum size:** 48 columns × 14 rows. Below that, WinTune shows a short
“terminal too small” message instead of a cramped layout.

---

## Themes (`--theme`)

| Theme     | Behavior                                      |
| --------- | --------------------------------------------- |
| `default` | Full gauges + CPU/RAM/disk sparklines         |
| `compact` | Shorter gauges, fewer process rows, no sparks |
| `mono`    | No ANSI colors (same as `--no-color` intent)  |

`--safe-terminal` / remote sessions still force conservative ASCII + no-color.

---

## Rendering Modes

| Mode             | Trigger                | Behavior                                  |
| ---------------- | ---------------------- | ----------------------------------------- |
| Unicode          | default                | Box-drawing glyphs, block bars, sparks    |
| ASCII fallback   | `--no-unicode`         | `+`, `-`, `|`, `#` characters             |
| No-color         | `--no-color` / `mono`  | No ANSI color sequences                   |
| Safe terminal    | `--safe-terminal`      | Conservative output for SSH/unknown terms |
| Non-interactive  | auto-detected          | Refuses TUI; suggests `scan`/`top`        |

---

## Layout (Unicode)

```text
WinTune Live   Host: DESKTOP-9KD2   Up: 3d 04h   Power: Balanced (AC)
──────────────────────────────────────────────────────────────────────
CPU   [███████████░░░░░░░░░░░░░░░░░]  38.0%
RAM   [██████████████████░░░░░░░░░░]  72.0%  23.1 GB / 32 GB
DISK  [███████████████████████░░░░░]  84.0%  active
cpu~  ▃▅▇▅▃▂▄▆▇▅▄▃▂▁▂▃▄▅▆▇▅▃
ram~  ▆▆▇▇▇▆▆▆▇▇▇▆▅▅▆▆▇▇▇▆▆▅
dsk~  ▁▂▃▅▇▅▃▂▁▂▃▄▅▃▂▁▂▃▄▅▃▂
NET   down 12.4 MB/s     up 1.1 MB/s
──────────────────────────────────────────────────────────────────────
── Top Processes ─────────────────────────────────────────────────────
sort: cpu  scroll: 1/64
PID    Process               CPU%     Memory       Disk
8420   chrome.exe            21.4%    2.4 GB       8.0 MB/s
...
──────────────────────────────────────────────────────────────────────
Keys: q quit | Space pause | t sort | j/k scroll | e export | ...
```

---

## Controls

```text
q / Esc / Ctrl+C   quit
Space              pause / resume refresh (freeze frame)
r                  refresh now (also resumes if paused)
o                  overview
d / m / n / p / s  disk / memory / network / power / services
t                  cycle sort: cpu → memory → disk
1 / 2 / 3          sort by CPU / memory / disk
j / k or arrows    scroll process or service list
PgUp / PgDn        page scroll
e                  export snapshot to Documents\WinTune\Reports\
? / h              help
```

Services stay on `s`. Snapshot export uses `e` so the keys do not conflict.

The TUI must **never** apply system changes without confirmation.

---

## Performance notes

- Process CPU/disk rates use a short **250 ms** sample (not a full refresh
  interval), so the dashboard stays responsive.
- While **paused**, metrics and process lists are frozen; gauges stop updating.
- Resize is detected each poll tick and redraws immediately.

---

## Snapshot export

Press `e` to write a text snapshot:

```text
%USERPROFILE%\Documents\WinTune\Reports\wintune-tui-YYYYMMDD-HHMMSS.txt
```

Contents: CPU/RAM/disk summary plus the current process table. Read-only; no
system changes.

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
```

Always restore the cursor and leave the alternate screen on exit.
