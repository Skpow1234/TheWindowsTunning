#include "cli/commands_recommend.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "system/updates.h"
#include "output/text.h"
#include "output/json.h"

#include <stdio.h>

int wt_cmd_recommend(const WT_CliOptions *opts)
{
    WT_ScanOptions scan_opts;
    scan_opts.cpu_sample_ms = (opts != NULL && opts->interval_ms > 0)
                                  ? (unsigned int)opts->interval_ms
                                  : 0;
    scan_opts.sample_count =
        (opts != NULL && opts->samples > 0) ? (unsigned int)opts->samples : 0;
    scan_opts.sample_interval_ms =
        (opts != NULL && opts->interval_ms > 0)
            ? (unsigned int)opts->interval_ms
            : 0;
    scan_opts.top_limit = 10;

    WT_ScanReport report;
    WT_Result r = wt_run_scan(&scan_opts, &report);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: scan failed (%s)\n", wt_result_to_string(r));
        return 1;
    }

    if (report.updates_ok) {
        (void)wt_search_pending_updates(&report.updates);
    }

    WT_RecommendationList recs;
    wt_generate_recommendations(&report, &recs);

    if (opts != NULL && opts->json) {
        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0
                    || opened == NULL) {
                fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                         opts->output_path);
                return 1;
            }
            out = opened;
        }

        wt_print_recommendations_json(&recs, out);

        if (opened != NULL) {
            fclose(opened);
        }
        return 0;
    }

    wt_print_recommendations_text(&recs);
    return 0;
}
