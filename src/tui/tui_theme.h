#ifndef WINTUNE_TUI_THEME_H
#define WINTUNE_TUI_THEME_H

#include <wchar.h>

/* Named TUI theme presets (Phase 20). */
typedef enum WT_TuiThemePreset {
    WT_TUI_THEME_DEFAULT = 0,
    WT_TUI_THEME_COMPACT,
    WT_TUI_THEME_MONO
} WT_TuiThemePreset;

/* Rendering style for the TUI: whether to use ANSI color and whether to use
 * Unicode box/bar glyphs (vs. an ASCII fallback). All glyphs are UTF-8 string
 * literals so they can be written directly to a UTF-8 console. */
typedef struct WT_TuiTheme {
    int color;    /* 1 = emit ANSI color escapes */
    int unicode;  /* 1 = Unicode glyphs, 0 = ASCII fallback */
    WT_TuiThemePreset preset;
    int gauge_width; /* default bar width (compact uses a shorter bar) */

    /* Gauge glyphs (single visible cell each). */
    const char *bar_full;
    const char *bar_empty;

    /* Sparkline glyphs (8 levels Unicode, or ASCII fallback). */
    const char *const *spark_levels;
    int spark_level_count;

    /* Box-drawing glyphs. */
    const char *h, *v, *tl, *tr, *bl, *br, *ml, *mr;
} WT_TuiTheme;

void wt_tui_theme_init(WT_TuiTheme *theme, int color, int unicode);

/* Applies a named preset on top of color/unicode mode.
 * Names: default | compact | mono. Unknown names fall back to default. */
void wt_tui_theme_apply_preset(WT_TuiTheme *theme, const wchar_t *preset_name);

/* ANSI escapes (return "" when color is disabled). */
const char *wt_tui_reset(const WT_TuiTheme *t);
const char *wt_tui_bold(const WT_TuiTheme *t);
const char *wt_tui_dim(const WT_TuiTheme *t);
const char *wt_tui_cyan(const WT_TuiTheme *t);

/* Color for a 0..100 utilization value: green / yellow / red. */
const char *wt_tui_color_for_pct(const WT_TuiTheme *t, double pct);

#endif /* WINTUNE_TUI_THEME_H */
