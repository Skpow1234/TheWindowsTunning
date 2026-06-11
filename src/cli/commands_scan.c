#include "cli/commands_scan.h"
#include "core/scan.h"
#include "output/text.h"

#include <stdio.h>

int wt_cmd_scan(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->json) {
        fprintf(stderr, "wintune: --json output is implemented in Phase 2.\n");
        return 1;
    }

    WT_ScanOptions scan_opts;
    scan_opts.cpu_sample_ms = (opts != NULL && opts->interval_ms > 0)
                                  ? (unsigned int)opts->interval_ms
                                  : 0; /* 0 -> scan default */
    scan_opts.top_limit = 10;

    WT_ScanReport report;
    WT_Result r = wt_run_scan(&scan_opts, &report);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: scan failed (%s)\n", wt_result_to_string(r));
        return 1;
    }

    wt_print_scan_report_text(&report);
    return 0;
}
