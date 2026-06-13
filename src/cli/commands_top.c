#include "cli/commands_top.h"
#include "cli/cli.h"
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
#define WT_TOP_DEFAULT_SAMPLE_MS 500
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
                                 WT_ProcessSort sort, unsigned int sample_ms,
                                 size_t *out_shown)
{
    size_t count = 0;
    WT_Result r =
        wt_collect_top_processes(buffer, limit, sort, sample_ms, &count);
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

static int wt_top_watch(size_t limit, WT_ProcessSort sort,
                        unsigned int interval_ms, unsigned int sample_ms)
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
        WT_Result r =
            wt_top_snapshot(buffer, limit, sort, sample_ms, &shown);

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
    const int json = wt_cli_is_json_mode(opts);
    const int watch = (opts != NULL && opts->watch);

    WT_ProcessSort sort = WT_PROCESS_SORT_MEMORY;
    if (opts != NULL && opts->sort != NULL) {
        if (_wcsicmp(opts->sort, L"cpu") == 0) {
            sort = WT_PROCESS_SORT_CPU;
        } else if (_wcsicmp(opts->sort, L"disk") == 0) {
            sort = WT_PROCESS_SORT_DISK;
        } else if (_wcsicmp(opts->sort, L"memory") != 0) {
            wt_cli_user_note(opts,
                             "wintune: unknown --sort key; using memory.\n");
        }
    }

    unsigned int sample_ms = WT_TOP_DEFAULT_SAMPLE_MS;
    if (sort != WT_PROCESS_SORT_MEMORY ||
            (opts != NULL && opts->sort != NULL)) {
        if (opts != NULL && opts->interval_ms > 0) {
            sample_ms = (unsigned int)opts->interval_ms;
        }
    }

    size_t limit = wt_top_clamp_limit(opts != NULL ? opts->limit : -1);

    if (watch && !json) {
        if (wt_session_is_interactive()) {
            unsigned int interval = WT_TOP_DEFAULT_INTERVAL_MS;
            if (opts != NULL && opts->interval_ms > 0) {
                interval = (unsigned int)opts->interval_ms;
            }
            return wt_top_watch(limit, sort, interval, sample_ms);
        }
        wt_cli_user_note(opts,
                         "wintune: 'top --watch' needs an interactive terminal; "
                         "showing a single snapshot.\n");
    } else if (watch && json) {
        /* Silent fallback in JSON mode: emit one snapshot, no stderr noise. */
    }

    WT_ProcessInfo *buffer = (WT_ProcessInfo *)malloc(limit * sizeof(WT_ProcessInfo));
    if (buffer == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    size_t shown = 0;
    WT_Result r = wt_top_snapshot(buffer, limit, sort, sample_ms, &shown);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: could not enumerate processes (%s)\n",
                wt_result_to_string(r));
        free(buffer);
        return 1;
    }

    if (json) {
        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts != NULL && opts->output_path != NULL) {
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
