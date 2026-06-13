#ifndef WINTUNE_CONSOLE_H
#define WINTUNE_CONSOLE_H

#include "common/error.h"

/* Console / terminal helpers built on the Windows console API and ANSI/VT
 * escape sequences. These are UI-agnostic and safe to call even when stdout is
 * redirected (they fail gracefully rather than corrupting piped output). */

/* Enables ENABLE_VIRTUAL_TERMINAL_PROCESSING on the stdout console so VT
 * escape sequences are interpreted. Returns WT_ERR_NOT_SUPPORTED if stdout is
 * not an interactive console. */
WT_Result wt_console_enable_vt(void);

/* Reports the visible console window size in character cells. */
WT_Result wt_console_get_size(int *rows, int *cols);

WT_Result wt_console_clear(void);
WT_Result wt_console_move_cursor(int row, int col); /* 1-based row/col */
WT_Result wt_console_hide_cursor(void);
WT_Result wt_console_show_cursor(void);

/* Returns 1 when stdout is an interactive console, 0 when redirected/piped
 * (e.g. `wintune scan > file` or an SSH non-interactive session). */
int wt_console_is_interactive(void);

/* Returns 1 when stdin is an interactive console (needed for prompts). */
int wt_console_stdin_is_interactive(void);

/* Returns 1 when both stdin and stdout are interactive consoles. Use this for
 * features that need a full terminal (TUI, --watch, confirmation prompts). */
int wt_session_is_interactive(void);

/* Returns 1 when the process appears to be running under SSH (OpenSSH sets
 * SSH_CONNECTION and/or SSH_CLIENT). Informational only. */
int wt_session_is_remote(void);

/* Returns 1 when the console can reasonably display ANSI color. Callers should
 * still honor a user-supplied --no-color override. */
int wt_console_supports_color(void);

/* Returns 1 when the process was likely started by double-clicking the .exe in
 * Explorer (parent is explorer.exe), as opposed to an existing shell. */
int wt_console_launched_from_explorer(void);

/* After printing help with no arguments, waits for Enter so Explorer-launched
 * sessions do not flash closed. No-op for shells (CMD, PowerShell, Git Bash). */
void wt_console_hold_open_if_explorer_launch(int argc);

#endif /* WINTUNE_CONSOLE_H */
