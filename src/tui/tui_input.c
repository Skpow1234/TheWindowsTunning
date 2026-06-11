#include "tui/tui_input.h"

#include <conio.h>

WT_TuiKey wt_tui_poll_key(void)
{
    if (!_kbhit()) {
        return WT_TUI_KEY_NONE;
    }

    int c = _getch();
    if (c == 0 || c == 224) {
        /* Function / arrow key: discard the second byte, ignore. */
        (void)_getch();
        return WT_TUI_KEY_NONE;
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
    default:                               return WT_TUI_KEY_NONE;
    }
}
