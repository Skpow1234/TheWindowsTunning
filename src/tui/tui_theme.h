#ifndef WINTUNE_TUI_THEME_H
#define WINTUNE_TUI_THEME_H

/* Rendering style for the TUI: whether to use ANSI color and whether to use
 * Unicode box/bar glyphs (vs. an ASCII fallback). All glyphs are UTF-8 string
 * literals so they can be written directly to a UTF-8 console. */
typedef struct WT_TuiTheme {
    int color;    /* 1 = emit ANSI color escapes */
    int unicode;  /* 1 = Unicode glyphs, 0 = ASCII fallback */

    /* Gauge glyphs (single visible cell each). */
    const char *bar_full;
    const char *bar_empty;

    /* Box-drawing glyphs. */
    const char *h, *v, *tl, *tr, *bl, *br, *ml, *mr;
} WT_TuiTheme;

void wt_tui_theme_init(WT_TuiTheme *theme, int color, int unicode);

/* ANSI escapes (return "" when color is disabled). */
const char *wt_tui_reset(const WT_TuiTheme *t);
const char *wt_tui_bold(const WT_TuiTheme *t);
const char *wt_tui_dim(const WT_TuiTheme *t);
const char *wt_tui_cyan(const WT_TuiTheme *t);

/* Color for a 0..100 utilization value: green / yellow / red. */
const char *wt_tui_color_for_pct(const WT_TuiTheme *t, double pct);

#endif /* WINTUNE_TUI_THEME_H */
