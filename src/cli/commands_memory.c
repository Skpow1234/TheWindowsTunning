#include "cli/commands_memory.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "common/error.h"
#include "common/units.h"
#include "metrics/memory.h"
#include "output/json.h"

#include <stdio.h>

static void wt_print_memory_text(const WT_MemoryMetrics *m)
{
    wchar_t buf[32];
    wchar_t buf2[32];
    wchar_t buf3[32];

    printf("Memory & commit charge (read-only)\n\n");

    printf("Physical RAM\n");
    wt_format_bytes(m->used_physical_bytes, buf, 32);
    wt_format_bytes(m->total_physical_bytes, buf2, 32);
    wt_format_bytes(m->available_physical_bytes, buf3, 32);
    fwprintf(stdout, L"  Used:       %ls / %ls (%.1f%%)\n", buf, buf2,
             m->used_percent);
    fwprintf(stdout, L"  Available:  %ls\n", buf3);

    printf("\nCommit charge\n");
    if (m->commit_ok) {
        wt_format_bytes(m->commit_total_bytes, buf, 32);
        wt_format_bytes(m->commit_limit_bytes, buf2, 32);
        wt_format_bytes(m->commit_peak_bytes, buf3, 32);
        fwprintf(stdout, L"  Current:    %ls / %ls (%.1f%%)\n", buf, buf2,
                 m->commit_percent);
        fwprintf(stdout, L"  Peak:       %ls\n", buf3);
        if (m->pagefile_ok) {
            wt_format_bytes(m->commit_available_bytes, buf, 32);
            fwprintf(stdout, L"  Available:  %ls commit headroom\n", buf);
        }
    } else {
        printf("  (unavailable)\n");
    }

    printf("\nPaging / hard faults\n");
    if (m->hard_faults_ok && m->hard_faults_per_sec >= 0.0) {
        printf("  Hard faults (Pages Input/sec): %.1f\n",
               m->hard_faults_per_sec);
    } else {
        printf("  Hard faults (Pages Input/sec): (not sampled)\n");
    }
    if (m->page_faults_ok && m->page_faults_per_sec >= 0.0) {
        printf("  Page faults/sec (soft+hard):   %.1f\n",
               m->page_faults_per_sec);
    }

    printf("\nWinTune never empties working sets or \"cleans RAM\".\n");
    printf("If commit charge is high: close or delay heavy apps "
           "(wintune top / startup).\n");
}

int wt_cmd_memory(const WT_CliOptions *opts)
{
    WT_MemoryMetrics m;
    unsigned sample_ms = 500;
    WT_Result r;

    if (opts != NULL && opts->interval_ms > 0) {
        sample_ms = (unsigned)opts->interval_ms;
    }

    r = wt_collect_memory_metrics_ex(&m, sample_ms);
    if (r != WT_OK) {
        return wt_cli_exit_from_result(opts, r, L"memory",
                                       "Could not collect memory metrics.");
    }

    if (opts != NULL && opts->json) {
        wt_cli_configure_json_output(opts);
        wt_print_memory_json(&m, stdout);
        return WT_EXIT_OK;
    }

    wt_print_memory_text(&m);
    return WT_EXIT_OK;
}
