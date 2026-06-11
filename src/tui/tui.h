#ifndef WINTUNE_TUI_H
#define WINTUNE_TUI_H

#include "cli/cli.h"
#include "common/error.h"

/* Runs the interactive live dashboard. Requires an interactive terminal;
 * returns WT_ERR_NOT_SUPPORTED otherwise. Always restores the terminal (cursor,
 * main screen buffer) on exit, including on Ctrl+C. Read-only: never changes
 * any system setting. */
WT_Result wt_tui_run(const WT_CliOptions *opts);

#endif /* WINTUNE_TUI_H */
