#include "tui/tui_theme.h"

void wt_tui_theme_init(WT_TuiTheme *theme, int color, int unicode)
{
    theme->color = color ? 1 : 0;
    theme->unicode = unicode ? 1 : 0;

    if (theme->unicode) {
        theme->bar_full  = "\xE2\x96\x88"; /* U+2588 FULL BLOCK */
        theme->bar_empty = "\xE2\x96\x91"; /* U+2591 LIGHT SHADE */
        theme->h  = "\xE2\x94\x80"; /* ─ */
        theme->v  = "\xE2\x94\x82"; /* │ */
        theme->tl = "\xE2\x94\x8C"; /* ┌ */
        theme->tr = "\xE2\x94\x90"; /* ┐ */
        theme->bl = "\xE2\x94\x94"; /* └ */
        theme->br = "\xE2\x94\x98"; /* ┘ */
        theme->ml = "\xE2\x94\x9C"; /* ├ */
        theme->mr = "\xE2\x94\xA4"; /* ┤ */
    } else {
        theme->bar_full  = "#";
        theme->bar_empty = "-";
        theme->h  = "-";
        theme->v  = "|";
        theme->tl = "+";
        theme->tr = "+";
        theme->bl = "+";
        theme->br = "+";
        theme->ml = "+";
        theme->mr = "+";
    }
}

const char *wt_tui_reset(const WT_TuiTheme *t) { return t->color ? "\x1b[0m" : ""; }
const char *wt_tui_bold(const WT_TuiTheme *t)  { return t->color ? "\x1b[1m" : ""; }
const char *wt_tui_dim(const WT_TuiTheme *t)   { return t->color ? "\x1b[2m" : ""; }
const char *wt_tui_cyan(const WT_TuiTheme *t)  { return t->color ? "\x1b[36m" : ""; }

const char *wt_tui_color_for_pct(const WT_TuiTheme *t, double pct)
{
    if (!t->color) {
        return "";
    }
    if (pct >= 85.0) {
        return "\x1b[31m"; /* red */
    }
    if (pct >= 60.0) {
        return "\x1b[33m"; /* yellow */
    }
    return "\x1b[32m"; /* green */
}
