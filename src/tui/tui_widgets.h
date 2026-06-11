#ifndef WINTUNE_TUI_WIDGETS_H
#define WINTUNE_TUI_WIDGETS_H

#include "tui/tui_screen.h"
#include "tui/tui_theme.h"

/* Appends a labelled gauge line:  "LABEL [#####-----]  38%  <suffix>".
 * `pct` is clamped to 0..100; `bar_width` is the number of bar cells. */
void wt_tui_gauge_line(WT_TuiScreen *s, const WT_TuiTheme *t,
                       const char *label, double pct, int bar_width,
                       const char *suffix);

/* Appends a horizontal rule line of `width` columns. */
void wt_tui_rule_line(WT_TuiScreen *s, const WT_TuiTheme *t, int width);

/* Appends a centered section title line, e.g. "── Top Processes ──". */
void wt_tui_title_line(WT_TuiScreen *s, const WT_TuiTheme *t,
                       const char *title, int width);

#endif /* WINTUNE_TUI_WIDGETS_H */
