#include "tui/tui_input.h"

#include <conio.h>

WT_TuiKey wt_tui_poll_key(void)
{
    if (!_kbhit()) {
        return WT_TUI_KEY_NONE;
    }

    int c = _getch();
    if (c == 0 || c == 224) {
        int ext = _getch();
        switch (ext) {
        case 72: return WT_TUI_KEY_SCROLL_UP;    /* Up */
        case 80: return WT_TUI_KEY_SCROLL_DOWN;  /* Down */
        case 73: return WT_TUI_KEY_PAGE_UP;      /* PgUp */
        case 81: return WT_TUI_KEY_PAGE_DOWN;    /* PgDn */
        default: return WT_TUI_KEY_NONE;
        }
    }

    switch (c) {
    case 'q': case 'Q': case 27 /* ESC */: return WT_TUI_KEY_QUIT;
    case 'r': case 'R':                    return WT_TUI_KEY_REFRESH;
    case '?': case 'h': case 'H':          return WT_TUI_KEY_HELP;
    case 'o': case 'O':                    return WT_TUI_KEY_OVERVIEW;
    case 'p': case 'P':                    return WT_TUI_KEY_POWER;
    case 's': case 'S':                    return WT_TUI_KEY_SERVICES;
    case 'd': case 'D':                    return WT_TUI_KEY_DISK;
    case 'm': case 'M':                    return WT_TUI_KEY_MEMORY;
    case 'n': case 'N':                    return WT_TUI_KEY_NETWORK;
    case ' ':                              return WT_TUI_KEY_PAUSE;
    case 't': case 'T':                    return WT_TUI_KEY_SORT_CYCLE;
    case '1':                              return WT_TUI_KEY_SORT_CPU;
    case '2':                              return WT_TUI_KEY_SORT_MEMORY;
    case '3':                              return WT_TUI_KEY_SORT_DISK;
    case 'j': case 'J':                    return WT_TUI_KEY_SCROLL_DOWN;
    case 'k': case 'K':                    return WT_TUI_KEY_SCROLL_UP;
    case 'e': case 'E':                    return WT_TUI_KEY_EXPORT;
    default:                               return WT_TUI_KEY_NONE;
    }
}
