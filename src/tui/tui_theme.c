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

static const char *const WT_SPARK_HC_ASCII[] = {
    " ", ".", ":", "=", "+", "*", "#", "@"
};

void wt_tui_theme_init(WT_TuiTheme *theme, int color, int unicode)
{
    theme->color = color ? 1 : 0;
    theme->unicode = unicode ? 1 : 0;
    theme->preset = WT_TUI_THEME_DEFAULT;
    theme->gauge_width = 30;
    theme->label_width = 5;
    theme->a11y_labels = 0;
    theme->skip_sparklines = 0;
    theme->safe_layout = 0;
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

static void wt_tui_theme_force_ascii_glyphs(WT_TuiTheme *theme)
{
    theme->unicode = 0;
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
    theme->spark_levels = WT_SPARK_ASCII;
    theme->spark_level_count = 8;
}

void wt_tui_theme_apply_preset(WT_TuiTheme *theme, const wchar_t *preset_name)
{
    if (theme == NULL) {
        return;
    }

    theme->preset = WT_TUI_THEME_DEFAULT;
    theme->gauge_width = 30;
    theme->label_width = 5;
    theme->a11y_labels = 0;
    theme->skip_sparklines = 0;
    theme->safe_layout = 0;

    if (preset_name == NULL || preset_name[0] == L'\0' ||
        _wcsicmp(preset_name, L"default") == 0) {
        return;
    }

    if (_wcsicmp(preset_name, L"compact") == 0) {
        theme->preset = WT_TUI_THEME_COMPACT;
        theme->gauge_width = 16;
        theme->skip_sparklines = 1;
        return;
    }

    if (_wcsicmp(preset_name, L"mono") == 0) {
        theme->preset = WT_TUI_THEME_MONO;
        theme->color = 0;
        return;
    }

    if (_wcsicmp(preset_name, L"high-contrast") == 0 ||
        _wcsicmp(preset_name, L"highcontrast") == 0 ||
        _wcsicmp(preset_name, L"hc") == 0) {
        theme->preset = WT_TUI_THEME_HIGH_CONTRAST;
        theme->color = 1;
        theme->a11y_labels = 1;
        theme->label_width = 10;
        theme->gauge_width = 24;
        theme->skip_sparklines = 1; /* sparks are hard for low vision */
        theme->bar_full = "=";
        theme->bar_empty = " ";
        theme->spark_levels = WT_SPARK_HC_ASCII;
        theme->spark_level_count = 8;
        /* Prefer ASCII chrome for predictable cell widths. */
        wt_tui_theme_force_ascii_glyphs(theme);
        theme->bar_full = "=";
        theme->bar_empty = " ";
        return;
    }

    if (_wcsicmp(preset_name, L"ssh") == 0 ||
        _wcsicmp(preset_name, L"safe") == 0) {
        theme->preset = WT_TUI_THEME_SSH;
        theme->color = 0;
        wt_tui_theme_force_ascii_glyphs(theme);
        theme->gauge_width = 14;
        theme->label_width = 10;
        theme->a11y_labels = 1;
        theme->skip_sparklines = 1;
        theme->safe_layout = 1;
        return;
    }
}

void wt_tui_theme_apply_safe_layout(WT_TuiTheme *theme)
{
    if (theme == NULL) {
        return;
    }

    int keep_hc_color =
        (theme->preset == WT_TUI_THEME_HIGH_CONTRAST && theme->color);

    wt_tui_theme_force_ascii_glyphs(theme);
    if (!keep_hc_color) {
        theme->color = 0;
    } else {
        theme->bar_full = "=";
        theme->bar_empty = " ";
    }

    theme->safe_layout = 1;
    theme->a11y_labels = 1;
    theme->skip_sparklines = 1;
    if (theme->label_width < 10) {
        theme->label_width = 10;
    }
    if (theme->gauge_width > 16) {
        theme->gauge_width = 16;
    }
    if (theme->preset == WT_TUI_THEME_DEFAULT ||
        theme->preset == WT_TUI_THEME_MONO) {
        theme->preset = WT_TUI_THEME_SSH;
    }
}

const char *wt_tui_reset(const WT_TuiTheme *t)
{
    return (t != NULL && t->color) ? "\x1b[0m" : "";
}

const char *wt_tui_bold(const WT_TuiTheme *t)
{
    return (t != NULL && t->color) ? "\x1b[1m" : "";
}

const char *wt_tui_dim(const WT_TuiTheme *t)
{
    if (t == NULL || !t->color) {
        return "";
    }
    /* Dim is hard to read; high-contrast / a11y use bold instead. */
    if (t->preset == WT_TUI_THEME_HIGH_CONTRAST || t->a11y_labels) {
        return "\x1b[1m";
    }
    return "\x1b[2m";
}

const char *wt_tui_cyan(const WT_TuiTheme *t)
{
    if (t == NULL || !t->color) {
        return "";
    }
    if (t->preset == WT_TUI_THEME_HIGH_CONTRAST) {
        return "\x1b[1;96m"; /* bright cyan */
    }
    return "\x1b[36m";
}

const char *wt_tui_color_for_pct(const WT_TuiTheme *t, double pct)
{
    if (t == NULL || !t->color) {
        return "";
    }
    if (t->preset == WT_TUI_THEME_HIGH_CONTRAST) {
        if (pct >= 85.0) {
            return "\x1b[1;97;41m"; /* bright white on red */
        }
        if (pct >= 60.0) {
            return "\x1b[1;30;43m"; /* black on yellow */
        }
        return "\x1b[1;97;42m"; /* bright white on green */
    }
    if (pct >= 85.0) {
        return "\x1b[31m"; /* red */
    }
    if (pct >= 60.0) {
        return "\x1b[33m"; /* yellow */
    }
    return "\x1b[32m"; /* green */
}
