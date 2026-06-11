#include "tui/tui_widgets.h"

#include <string.h>

void wt_tui_gauge_line(WT_TuiScreen *s, const WT_TuiTheme *t,
                       const char *label, double pct, int bar_width,
                       const char *suffix)
{
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    if (bar_width < 1) bar_width = 1;

    int filled = (int)((pct / 100.0) * bar_width + 0.5);
    if (filled > bar_width) filled = bar_width;

    const char *color = wt_tui_color_for_pct(t, pct);
    const char *reset = wt_tui_reset(t);

    /* Build the bar segment manually (glyphs may be multibyte UTF-8). */
    char bar[512];
    size_t pos = 0;
    bar[0] = '\0';

    size_t color_len = strlen(color);
    if (pos + color_len < sizeof(bar)) {
        memcpy(bar + pos, color, color_len);
        pos += color_len;
    }
    for (int i = 0; i < bar_width; ++i) {
        const char *glyph = (i < filled) ? t->bar_full : t->bar_empty;
        size_t gl = strlen(glyph);
        if (pos + gl >= sizeof(bar) - 16) {
            break;
        }
        memcpy(bar + pos, glyph, gl);
        pos += gl;
    }
    size_t reset_len = strlen(reset);
    if (pos + reset_len < sizeof(bar)) {
        memcpy(bar + pos, reset, reset_len);
        pos += reset_len;
    }
    bar[pos] = '\0';

    wt_tui_screen_line(s, "%-5s [%s] %5.1f%%  %s",
                       label, bar, pct, suffix ? suffix : "");
}

void wt_tui_rule_line(WT_TuiScreen *s, const WT_TuiTheme *t, int width)
{
    if (width < 1) width = 1;
    if (width > 200) width = 200;

    char line[1024];
    size_t pos = 0;
    size_t hl = strlen(t->h);
    for (int i = 0; i < width; ++i) {
        if (pos + hl >= sizeof(line) - 1) {
            break;
        }
        memcpy(line + pos, t->h, hl);
        pos += hl;
    }
    line[pos] = '\0';
    wt_tui_screen_line(s, "%s%s%s", wt_tui_dim(t), line, wt_tui_reset(t));
}

void wt_tui_title_line(WT_TuiScreen *s, const WT_TuiTheme *t,
                       const char *title, int width)
{
    if (title == NULL) title = "";
    int title_len = (int)strlen(title);
    int dashes = width - title_len - 2;
    if (dashes < 2) dashes = 2;
    int left = dashes / 2;
    int right = dashes - left;
    if (left > 200) left = 200;
    if (right > 200) right = 200;

    char buf[1024];
    size_t pos = 0;
    size_t hl = strlen(t->h);
    for (int i = 0; i < left && pos + hl < sizeof(buf) - 1; ++i) {
        memcpy(buf + pos, t->h, hl);
        pos += hl;
    }
    buf[pos] = '\0';

    char tail[512];
    size_t tpos = 0;
    for (int i = 0; i < right && tpos + hl < sizeof(tail) - 1; ++i) {
        memcpy(tail + tpos, t->h, hl);
        tpos += hl;
    }
    tail[tpos] = '\0';

    wt_tui_screen_line(s, "%s%s %s%s%s %s%s%s",
                       wt_tui_dim(t), buf,
                       wt_tui_bold(t), title, wt_tui_reset(t),
                       wt_tui_dim(t), tail, wt_tui_reset(t));
}
