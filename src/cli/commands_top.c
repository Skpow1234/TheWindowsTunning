#include "cli/commands_top.h"
#include "metrics/process.h"
#include "output/table.h"
#include "output/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define WT_TOP_DEFAULT_LIMIT 12
#define WT_TOP_PROCESS_SCAN_CAP 2048

int wt_cmd_top(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->watch) {
        fprintf(stderr,
                "wintune: 'top --watch' is implemented in Phase 3; "
                "showing a single snapshot.\n");
    }

    if (opts != NULL && opts->sort != NULL && wcscmp(opts->sort, L"memory") != 0) {
        fprintf(stderr,
                "wintune: only --sort memory is available in Phase 1; "
                "sorting by memory.\n");
    }

    size_t limit = WT_TOP_DEFAULT_LIMIT;
    if (opts != NULL && opts->limit > 0) {
        limit = (size_t)opts->limit;
    }

    WT_ProcessInfo *all =
        (WT_ProcessInfo *)malloc(WT_TOP_PROCESS_SCAN_CAP * sizeof(WT_ProcessInfo));
    if (all == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    size_t count = 0;
    WT_Result r = wt_collect_processes(all, WT_TOP_PROCESS_SCAN_CAP, &count);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: could not enumerate processes (%s)\n",
                wt_result_to_string(r));
        free(all);
        return 1;
    }

    wt_sort_processes_by_memory(all, count);
    if (limit > count) {
        limit = count;
    }

    if (opts != NULL && opts->json) {
        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 || opened == NULL) {
                fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                         opts->output_path);
                free(all);
                return 1;
            }
            out = opened;
        }

        wt_print_processes_json(all, limit, out);

        if (opened != NULL) {
            fclose(opened);
        }
        free(all);
        return 0;
    }

    wt_print_process_table(all, limit);
    free(all);
    return 0;
}
