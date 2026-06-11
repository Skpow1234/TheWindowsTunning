#include "tui/tui_screen.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define WT_TUI_SCREEN_INITIAL_CAP 8192

static int wt_tui_screen_reserve(WT_TuiScreen *s, size_t extra)
{
    if (s->len + extra + 1 <= s->cap) {
        return 1;
    }
    size_t new_cap = s->cap ? s->cap : WT_TUI_SCREEN_INITIAL_CAP;
    while (s->len + extra + 1 > new_cap) {
        new_cap *= 2;
    }
    char *grown = (char *)realloc(s->buf, new_cap);
    if (grown == NULL) {
        return 0;
    }
    s->buf = grown;
    s->cap = new_cap;
    return 1;
}

WT_Result wt_tui_screen_init(WT_TuiScreen *s)
{
    if (s == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    s->buf = (char *)malloc(WT_TUI_SCREEN_INITIAL_CAP);
    if (s->buf == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    s->cap = WT_TUI_SCREEN_INITIAL_CAP;
    s->len = 0;
    s->buf[0] = '\0';
    return WT_OK;
}

void wt_tui_screen_free(WT_TuiScreen *s)
{
    if (s != NULL && s->buf != NULL) {
        free(s->buf);
        s->buf = NULL;
        s->cap = 0;
        s->len = 0;
    }
}

void wt_tui_screen_reset(WT_TuiScreen *s)
{
    s->len = 0;
    if (s->buf != NULL) {
        s->buf[0] = '\0';
    }
}

void wt_tui_screen_append(WT_TuiScreen *s, const char *text)
{
    if (text == NULL) {
        return;
    }
    size_t n = strlen(text);
    if (!wt_tui_screen_reserve(s, n)) {
        return;
    }
    memcpy(s->buf + s->len, text, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

void wt_tui_screen_line(WT_TuiScreen *s, const char *fmt, ...)
{
    char line[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n < 0) {
        line[0] = '\0';
    }

    wt_tui_screen_append(s, line);
    wt_tui_screen_append(s, "\x1b[K\r\n"); /* erase to EOL, then newline */
}

void wt_tui_screen_flush(WT_TuiScreen *s, FILE *out)
{
    fputs("\x1b[H", out);              /* cursor home */
    if (s->len > 0) {
        fwrite(s->buf, 1, s->len, out);
    }
    fputs("\x1b[J", out);              /* erase from cursor to end of display */
    fflush(out);
}
