#ifndef WINTUNE_TUI_SCREEN_H
#define WINTUNE_TUI_SCREEN_H

#include <stddef.h>
#include <stdio.h>

#include "common/error.h"

/* A growable text buffer for one frame. The whole frame is built in memory and
 * written in a single flush to minimize flicker (important over SSH). Each line
 * is terminated with an erase-to-end-of-line so stale characters from a longer
 * previous frame are cleared without a full screen wipe. */
typedef struct WT_TuiScreen {
    char *buf;
    size_t len;
    size_t cap;
} WT_TuiScreen;

WT_Result wt_tui_screen_init(WT_TuiScreen *s);
void wt_tui_screen_free(WT_TuiScreen *s);
void wt_tui_screen_reset(WT_TuiScreen *s);

/* Appends raw bytes (no line handling). */
void wt_tui_screen_append(WT_TuiScreen *s, const char *text);

/* Appends one formatted line followed by erase-to-EOL and CRLF. */
void wt_tui_screen_line(WT_TuiScreen *s, const char *fmt, ...);

/* Moves the cursor home, writes the buffer, then erases from the cursor to the
 * end of the display, and flushes. */
void wt_tui_screen_flush(WT_TuiScreen *s, FILE *out);

#endif /* WINTUNE_TUI_SCREEN_H */
