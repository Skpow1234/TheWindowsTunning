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
wintune tui --theme high-contrast
wintune tui --theme ssh
wintune tui --safe-terminal --theme high-contrast
wintune tui --sort cpu
wintune tui --include-network
```

The TUI must work in Windows Terminal, PowerShell, CMD (with VT enabled), the
VS Code terminal, and SSH sessions.

**Minimum size:** 48 columns × 14 rows (36×12 in safe / SSH layout). Below that,
WinTune shows a short “terminal too small” message instead of a cramped layout.

---

## Themes (`--theme`)

| Theme            | Behavior                                                       |
| ---------------- | -------------------------------------------------------------- |
| `default`        | Full gauges + CPU/RAM/disk/net sparklines                      |
| `compact`        | Shorter gauges, fewer process rows, no sparks                  |
| `mono`           | No ANSI colors (same as `--no-color` intent)                   |
| `high-contrast`  | Bright on-bar colors, ASCII chrome, longer a11y labels (`hc`)  |
| `ssh` / `safe`   | ASCII, no color, compact chrome, a11y labels (SSH-friendly)    |

`--safe-terminal` / remote sessions force ASCII + compact chrome and clearer
labels. Pair with `--theme high-contrast` to keep high-visibility colors over
SSH (unless `--no-color`).

---

## Accessibility (Phase 43)

When a11y labels are on (`high-contrast`, `ssh`, or `--safe-terminal`):

- Header uses **Host name**, **Uptime**, **Power plan**
- Gauges use **CPU usage**, **Memory**, **Disk act.**, **Network**
- Section titles are longer (for screen readers / linear reading)
- Dim text is avoided (bold used instead where color is available)
- Footer shortens on narrow / safe layouts

Keyboard-only; no mouse required.

---

## Rendering Modes

| Mode             | Trigger                | Behavior                                  |
| ---------------- | ---------------------- | ----------------------------------------- |
| Unicode          | default                | Box-drawing glyphs, block bars, sparks    |
| ASCII fallback   | `--no-unicode`         | `+`, `-`, `|`, `#` characters             |
| No-color         | `--no-color` / `mono`  | No ANSI color sequences                   |
| Safe terminal    | `--safe-terminal`      | ASCII + compact + a11y labels for SSH     |
| High contrast    | `--theme high-contrast`| Bright utilization colors; no sparks      |
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
Keys: q quit | Space pause | t sort | w net | j/k scroll | e export | ...
```

---

## Controls

```text
q / Esc / Ctrl+C   quit
Space              pause / resume refresh (freeze frame)
r                  refresh now (also resumes if paused)
o                  overview
d / m / n / g / p / s  disk / memory / network / GPU / power / services
t                  cycle sort: cpu → memory → disk → net
1 / 2 / 3 / 4      sort by CPU / memory / disk / network
w                  toggle per-process TCP net columns (extra overhead)
j / k or arrows    scroll process or service list
PgUp / PgDn        page scroll
e                  export snapshot to Documents\WinTune\Reports\
b                  mark before snapshot (for compare)
a                  mark after snapshot
c                  before/after compare view (measured deltas only)
? / h              help
```

Services stay on `s`. Snapshot export uses `e` so the keys do not conflict.
Per-process net columns use TCP Extended Stats only (no payload capture; UDP
not included) and stay off until `w` or `--include-network`.

The TUI must **never** apply system changes without confirmation.

---

## Before / after compare

Press `b` to capture a before snapshot of the current gauges, apply a change
outside the TUI (or with `wintune apply`), then press `a` for after, then `c`
to open the compare view.

Snapshots persist under:

```text
%LOCALAPPDATA%\WinTune\compare\before.json
%LOCALAPPDATA%\WinTune\compare\after.json
```

Deltas are **sample-window differences only** (percentage points for CPU/RAM/disk,
B/s for net). They are not lasting gains. `wintune report` includes the pair when
both or either snapshot exists on disk.

---

## Performance notes

- Process CPU/disk rates use a short **250 ms** sample (not a full refresh
  interval), so the dashboard stays responsive.
- While **paused**, metrics and process lists are frozen; gauges stop updating.
- Resize is detected each poll tick and redraws immediately.

---

## Snapshot export

Press `e` to write a snapshot triad under Documents:

```text
%USERPROFILE%\Documents\WinTune\Reports\wintune-tui-YYYYMMDD-HHMMSS.txt
%USERPROFILE%\Documents\WinTune\Reports\wintune-tui-YYYYMMDD-HHMMSS.json
%USERPROFILE%\Documents\WinTune\Reports\wintune-tui-YYYYMMDD-HHMMSS.csv
```

- `.txt` — current gauges, in-session history table, process table
- `.json` — `current` + `history[]` (cpu/mem/disk %, net rx/tx bytes/sec)
- `.csv` — history timeseries only

Read-only; no system changes. History is a ring of the last 24 refresh samples
(CPU/RAM/disk percent + net down/up rates). Net sparklines (`dn~` / `up~`) are
scaled relative to the window peak.

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
