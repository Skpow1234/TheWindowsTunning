#include "cli/commands_top.h"
#include "metrics/process.h"
#include "output/table.h"
#include "output/json.h"
#include "platform/console.h"
#include "platform/time.h"

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define WT_TOP_DEFAULT_LIMIT 12
#define WT_TOP_MAX_LIMIT 128
#define WT_TOP_DEFAULT_INTERVAL_MS 1000
#define WT_TOP_POLL_STEP_MS 50

static volatile int g_watch_stop = 0;

static BOOL WINAPI wt_top_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        g_watch_stop = 1;
        return TRUE;
    default:
        return FALSE;
    }
}

static size_t wt_top_clamp_limit(long requested)
{
    size_t limit = WT_TOP_DEFAULT_LIMIT;
    if (requested > 0) {
        limit = (size_t)requested;
    }
    if (limit > WT_TOP_MAX_LIMIT) {
        limit = WT_TOP_MAX_LIMIT;
    }
    return limit;
}

static WT_Result wt_top_snapshot(WT_ProcessInfo *buffer, size_t limit,
                                 size_t *out_shown)
{
    size_t count = 0;
    WT_Result r = wt_collect_top_processes_by_memory(buffer, limit, &count);
    if (r != WT_OK) {
        return r;
    }
    *out_shown = count;
    return WT_OK;
}

static int wt_top_wait_or_quit(unsigned int ms)
{
    unsigned int waited = 0;
    while (waited < ms) {
        if (g_watch_stop) {
            return 1;
        }
        if (_kbhit()) {
            int c = _getch();
            if (c == 0 || c == 224) {
                (void)_getch();
            } else if (c == 'q' || c == 'Q') {
                return 1;
            }
        }
        Sleep(WT_TOP_POLL_STEP_MS);
        waited += WT_TOP_POLL_STEP_MS;
    }
    return 0;
}

static int wt_top_watch(size_t limit, unsigned int interval_ms)
{
    WT_ProcessInfo *buffer = (WT_ProcessInfo *)malloc(limit * sizeof(WT_ProcessInfo));
    if (buffer == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    (void)wt_console_enable_vt();
    SetConsoleCtrlHandler(wt_top_ctrl_handler, TRUE);
    g_watch_stop = 0;

    fputs("\x1b[?25l", stdout);

    int rc = 0;
    while (!g_watch_stop) {
        size_t shown = 0;
        WT_Result r = wt_top_snapshot(buffer, limit, &shown);

        fputs("\x1b[H\x1b[2J", stdout);

        char ts[32];
        if (wt_now_iso8601_utc(ts, sizeof(ts)) != WT_OK) {
            ts[0] = '\0';
        }
        printf("WinTune top   %s   refresh %ums   (press q or Ctrl+C to quit)\n\n",
               ts, interval_ms);

        if (r == WT_OK) {
            wt_print_process_table(buffer, shown);
        } else {
            printf("(could not enumerate processes: %s)\n", wt_result_to_string(r));
        }
        fflush(stdout);

        if (wt_top_wait_or_quit(interval_ms)) {
            break;
        }
    }

    fputs("\x1b[?25h", stdout);
    fputc('\n', stdout);
    fflush(stdout);

    SetConsoleCtrlHandler(wt_top_ctrl_handler, FALSE);
    free(buffer);
    return rc;
}

int wt_cmd_top(const WT_CliOptions *opts)
{
    const int json = (opts != NULL && opts->json);
    const int watch = (opts != NULL && opts->watch);

    if (opts != NULL && opts->sort != NULL && wcscmp(opts->sort, L"memory") != 0) {
        fprintf(stderr,
                "wintune: only --sort memory is available in Phase 1; "
                "sorting by memory.\n");
    }

    size_t limit = wt_top_clamp_limit(opts != NULL ? opts->limit : -1);

    if (watch && !json) {
        if (wt_console_is_interactive()) {
            unsigned int interval = WT_TOP_DEFAULT_INTERVAL_MS;
            if (opts->interval_ms > 0) {
                interval = (unsigned int)opts->interval_ms;
            }
            return wt_top_watch(limit, interval);
        }
        fprintf(stderr,
                "wintune: 'top --watch' needs an interactive terminal; "
                "showing a single snapshot.\n");
    } else if (watch && json) {
        fprintf(stderr,
                "wintune: 'top --watch' is ignored with --json; "
                "emitting a single snapshot.\n");
    }

    WT_ProcessInfo *buffer = (WT_ProcessInfo *)malloc(limit * sizeof(WT_ProcessInfo));
    if (buffer == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    size_t shown = 0;
    WT_Result r = wt_top_snapshot(buffer, limit, &shown);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: could not enumerate processes (%s)\n",
                wt_result_to_string(r));
        free(buffer);
        return 1;
    }

    if (json) {
        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 || opened == NULL) {
                fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                         opts->output_path);
                free(buffer);
                return 1;
            }
            out = opened;
        }
        wt_print_processes_json(buffer, shown, out);
        if (opened != NULL) {
            fclose(opened);
        }
    } else {
        wt_print_process_table(buffer, shown);
    }

    free(buffer);
    return 0;
}
