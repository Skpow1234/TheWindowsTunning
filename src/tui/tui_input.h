#ifndef WINTUNE_TUI_INPUT_H
#define WINTUNE_TUI_INPUT_H

typedef enum WT_TuiKey {
    WT_TUI_KEY_NONE = 0,
    WT_TUI_KEY_QUIT,
    WT_TUI_KEY_REFRESH,
    WT_TUI_KEY_HELP,
    WT_TUI_KEY_OVERVIEW,
    WT_TUI_KEY_POWER,
    WT_TUI_KEY_SERVICES,
    WT_TUI_KEY_DISK,
    WT_TUI_KEY_MEMORY,
    WT_TUI_KEY_NETWORK,
    WT_TUI_KEY_PAUSE,
    WT_TUI_KEY_SORT_CYCLE,
    WT_TUI_KEY_SORT_CPU,
    WT_TUI_KEY_SORT_MEMORY,
    WT_TUI_KEY_SORT_DISK,
    WT_TUI_KEY_SCROLL_UP,
    WT_TUI_KEY_SCROLL_DOWN,
    WT_TUI_KEY_PAGE_UP,
    WT_TUI_KEY_PAGE_DOWN,
    WT_TUI_KEY_EXPORT
} WT_TuiKey;

/* Non-blocking: returns the next mapped key press, or WT_TUI_KEY_NONE if no key
 * is pending. Unrecognized keys are consumed and reported as NONE. */
WT_TuiKey wt_tui_poll_key(void);

#endif /* WINTUNE_TUI_INPUT_H */
