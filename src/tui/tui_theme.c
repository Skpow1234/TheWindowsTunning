#include "tui/tui_theme.h"

#include <wchar.h>

static const char *const WT_SPARK_UNICODE[] = {
    "\xE2\x96\x81", /* ▁ */
    "\xE2\x96\x82", /* ▂ */
    "\xE2\x96\x83", /* ▃ */
    "\xE2\x96\x84", /* ▄ */
    "\xE2\x96\x85", /* ▅ */
    "\xE2\x96\x86", /* ▆ */
    "\xE2\x96\x87", /* ▇ */
    "\xE2\x96\x88"  /* █ */
};

static const char *const WT_SPARK_ASCII[] = {
    "_", ".", "-", "=", "+", "*", "#", "#"
};

void wt_tui_theme_init(WT_TuiTheme *theme, int color, int unicode)
{
    theme->color = color ? 1 : 0;
    theme->unicode = unicode ? 1 : 0;
    theme->preset = WT_TUI_THEME_DEFAULT;
    theme->gauge_width = 30;
    theme->spark_levels = theme->unicode ? WT_SPARK_UNICODE : WT_SPARK_ASCII;
    theme->spark_level_count = 8;

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

void wt_tui_theme_apply_preset(WT_TuiTheme *theme, const wchar_t *preset_name)
{
    if (theme == NULL) {
        return;
    }

    theme->preset = WT_TUI_THEME_DEFAULT;
    theme->gauge_width = 30;

    if (preset_name == NULL || preset_name[0] == L'\0' ||
        _wcsicmp(preset_name, L"default") == 0) {
        return;
    }

    if (_wcsicmp(preset_name, L"compact") == 0) {
        theme->preset = WT_TUI_THEME_COMPACT;
        theme->gauge_width = 16;
        return;
    }

    if (_wcsicmp(preset_name, L"mono") == 0) {
        theme->preset = WT_TUI_THEME_MONO;
        theme->color = 0;
        return;
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
