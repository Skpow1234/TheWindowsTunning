#include "cli/commands_top.h"
#include "cli/cli.h"
#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
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
#define WT_TOP_MAX_LIMIT 32
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
                                 int include_network, size_t *out_shown)
{
    size_t count = 0;
    WT_Result r =
        wt_collect_top_processes(buffer, limit, sort, sample_ms,
                                 include_network, &count);
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

static void wt_top_print_page_banner(const WT_ProcessPageInfo *page)
{
    if (page == NULL || page->total == 0) {
        printf("Processes: 0 (page %zu of %zu)\n\n",
               page != NULL ? page->page : 1,
               page != NULL ? page->page_count : 1);
        return;
    }
    size_t first = ((page->page - 1) * page->page_size) + 1;
    size_t shown = page->page_size;
    if (first + shown - 1 > page->total) {
        shown = page->total - first + 1;
    }
    printf("Processes %zu-%zu of %zu (page %zu of %zu)",
           first, first + shown - 1, page->total, page->page, page->page_count);
    if (page->truncated) {
        printf("  [list truncated at %d]", WT_PROCESS_ENUM_MAX);
    }
    printf("\n\n");
}

static WT_Result wt_top_page_snapshot(WT_ProcessInfo *buffer, size_t page_size,
                                      size_t page, WT_ProcessSort sort,
                                      unsigned int sample_ms, int include_network,
                                      size_t *out_shown, WT_ProcessPageInfo *info)
{
    return wt_collect_process_page(buffer, page_size, page, page_size, sort,
                                   sample_ms, include_network, out_shown, info);
}

static int wt_top_watch(size_t limit, WT_ProcessSort sort,
                        unsigned int interval_ms, unsigned int sample_ms,
                        int include_network, int paged, size_t page,
                        size_t page_size)
{
    size_t rows = paged ? page_size : limit;
    WT_ProcessInfo *buffer = (WT_ProcessInfo *)malloc(rows * sizeof(WT_ProcessInfo));
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
        WT_ProcessPageInfo page_info;
        ZeroMemory(&page_info, sizeof(page_info));
        WT_Result r;
        if (paged) {
            r = wt_top_page_snapshot(buffer, page_size, page, sort, sample_ms,
                                     include_network, &shown, &page_info);
        } else {
            r = wt_top_snapshot(buffer, limit, sort, sample_ms, include_network,
                                &shown);
        }

        fputs("\x1b[H\x1b[2J", stdout);

        char ts[32];
        if (wt_now_iso8601_utc(ts, sizeof(ts)) != WT_OK) {
            ts[0] = '\0';
        }
        printf("WinTune top   %s   refresh %ums   (press q or Ctrl+C to quit)\n\n",
               ts, interval_ms);

        if (r == WT_OK) {
            if (paged) {
                wt_top_print_page_banner(&page_info);
            }
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

static int wt_top_ndjson_stream(const WT_CliOptions *opts, size_t limit,
                                WT_ProcessSort sort, unsigned int sample_ms,
                                unsigned int interval_ms,
                                unsigned int duration_ms,
                                int include_network, int paged, size_t page,
                                size_t page_size)
{
    size_t rows = paged ? page_size : limit;
    WT_ProcessInfo *buffer =
        (WT_ProcessInfo *)malloc(rows * sizeof(WT_ProcessInfo));
    if (buffer == NULL) {
        return wt_cli_exit_from_result(opts, WT_ERR_OUT_OF_MEMORY, L"top",
                                       "Out of memory.");
    }

    wt_cli_configure_json_output(opts);

    FILE *out = stdout;
    FILE *opened = NULL;
    if (opts != NULL && opts->output_path != NULL) {
        if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 || opened == NULL) {
            free(buffer);
            return wt_cli_exit_from_result(opts, WT_ERR_WIN32, L"top",
                                           "Could not open output file.");
        }
        out = opened;
    }

    unsigned int elapsed = 0;
    for (;;) {
        size_t shown = 0;
        WT_ProcessPageInfo page_info;
        ZeroMemory(&page_info, sizeof(page_info));
        WT_Result r;
        if (paged) {
            r = wt_top_page_snapshot(buffer, page_size, page, sort, sample_ms,
                                     include_network, &shown, &page_info);
        } else {
            r = wt_top_snapshot(buffer, limit, sort, sample_ms, include_network,
                                &shown);
        }
        if (r != WT_OK) {
            free(buffer);
            if (opened != NULL) {
                fclose(opened);
            }
            return wt_cli_exit_from_result(opts, r, L"top",
                                           "Could not enumerate processes.");
        }

        if (paged) {
            wt_print_processes_page_json(buffer, shown, &page_info, out);
        } else {
            wt_print_processes_json(buffer, shown, out);
        }
        fflush(out);

        if (duration_ms == 0) {
            break;
        }
        elapsed += interval_ms;
        if (elapsed >= duration_ms) {
            break;
        }
        Sleep(interval_ms);
    }

    if (opened != NULL) {
        fclose(opened);
    }
    free(buffer);
    return WT_EXIT_OK;
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
        } else if (_wcsicmp(opts->sort, L"network") == 0 ||
                   _wcsicmp(opts->sort, L"net") == 0) {
            sort = WT_PROCESS_SORT_NETWORK;
        } else if (_wcsicmp(opts->sort, L"memory") != 0) {
            wt_cli_user_note(opts,
                             "wintune: unknown --sort key; using memory.\n");
        }
    }

    int include_network = (opts != NULL && opts->include_network) ||
                          (sort == WT_PROCESS_SORT_NETWORK);
    if (include_network && !json) {
        wt_cli_user_note(
            opts,
            "wintune: --include-network samples TCP Extended Stats "
            "(extra overhead; no payload capture; UDP not included).\n");
    }

    unsigned int sample_ms = WT_TOP_DEFAULT_SAMPLE_MS;
    if (sort != WT_PROCESS_SORT_MEMORY || include_network ||
            (opts != NULL && opts->sort != NULL)) {
        if (opts != NULL && opts->interval_ms > 0) {
            sample_ms = (unsigned int)opts->interval_ms;
        }
    }

    size_t limit = wt_top_clamp_limit(opts != NULL ? opts->limit : -1);

    int paged = 0;
    size_t page = 1;
    size_t page_size = WT_PROCESS_PAGE_MAX;
    if (opts != NULL && opts->page >= 0) {
        if (opts->page < 1) {
            fprintf(stderr, "wintune: --page must be 1 or greater\n");
            return WT_EXIT_USAGE;
        }
        paged = 1;
        page = (size_t)opts->page;
        if (opts->page_size > (long)WT_PROCESS_PAGE_MAX) {
            page_size = WT_PROCESS_PAGE_MAX;
            wt_cli_user_note(opts, "wintune: --page-size is capped at 32.\n");
        } else if (opts->page_size > 0) {
            page_size = (size_t)opts->page_size;
        } else if (opts->page_size == 0) {
            fprintf(stderr, "wintune: --page-size must be 1 or greater\n");
            return WT_EXIT_USAGE;
        }
    } else if (opts != NULL && opts->page_size >= 0) {
        fprintf(stderr, "wintune: --page-size requires --page\n");
        return WT_EXIT_USAGE;
    }

    if (watch && json && opts != NULL && opts->ndjson) {
        unsigned int interval = WT_TOP_DEFAULT_INTERVAL_MS;
        if (opts->interval_ms > 0) {
            interval = (unsigned int)opts->interval_ms;
        }
        unsigned int duration = 0;
        if (opts->duration_ms > 0) {
            duration = (unsigned int)opts->duration_ms;
        } else if (!wt_session_is_interactive()) {
            duration = interval;
        } else {
            duration = interval * 60u;
        }
        return wt_top_ndjson_stream(opts, limit, sort, sample_ms, interval,
                                    duration, include_network, paged, page,
                                    page_size);
    }

    if (watch && !json) {
        if (wt_session_is_interactive()) {
            unsigned int interval = WT_TOP_DEFAULT_INTERVAL_MS;
            if (opts != NULL && opts->interval_ms > 0) {
                interval = (unsigned int)opts->interval_ms;
            }
            return wt_top_watch(limit, sort, interval, sample_ms,
                                include_network, paged, page, page_size);
        }
        wt_cli_user_note(opts,
                         "wintune: 'top --watch' needs an interactive terminal; "
                         "showing a single snapshot.\n");
    } else if (watch && json) {
        /* Silent fallback in JSON mode: emit one snapshot, no stderr noise. */
    }

    size_t rows = paged ? page_size : limit;
    WT_ProcessInfo *buffer = (WT_ProcessInfo *)malloc(rows * sizeof(WT_ProcessInfo));
    if (buffer == NULL) {
        return wt_cli_exit_from_result(opts, WT_ERR_OUT_OF_MEMORY, L"top",
                                       "Out of memory.");
    }

    size_t shown = 0;
    WT_ProcessPageInfo page_info;
    ZeroMemory(&page_info, sizeof(page_info));
    WT_Result r;
    if (paged) {
        r = wt_top_page_snapshot(buffer, page_size, page, sort, sample_ms,
                                 include_network, &shown, &page_info);
    } else {
        r = wt_top_snapshot(buffer, limit, sort, sample_ms, include_network,
                            &shown);
    }
    if (r != WT_OK) {
        free(buffer);
        if (r == WT_ERR_NOT_FOUND) {
            fprintf(stderr,
                    "wintune: that page is past the end of the process list.\n");
            return wt_cli_exit_from_result(
                opts, r, L"top",
                "That page is past the end of the process list.");
        }
        return wt_cli_exit_from_result(opts, r, L"top",
                                       "Could not enumerate processes.");
    }

    if (json) {
        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts != NULL && opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 || opened == NULL) {
                free(buffer);
                return wt_cli_exit_from_result(opts, WT_ERR_WIN32, L"top",
                                               "Could not open output file.");
            }
            out = opened;
        }
        wt_cli_configure_json_output(opts);
        if (paged) {
            wt_print_processes_page_json(buffer, shown, &page_info, out);
        } else {
            wt_print_processes_json(buffer, shown, out);
        }
        if (opened != NULL) {
            fclose(opened);
        }
    } else {
        if (paged) {
            wt_top_print_page_banner(&page_info);
        }
        wt_print_process_table(buffer, shown);
    }

    free(buffer);
    return WT_EXIT_OK;
}
