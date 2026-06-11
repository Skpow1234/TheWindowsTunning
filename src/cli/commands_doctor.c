#include "cli/commands_doctor.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "output/text.h"
#include "output/json.h"

#include <stdio.h>

static void wt_print_doctor_summary(const WT_RecommendationList *recs)
{
    size_t high = 0, medium = 0, low = 0;
    for (size_t i = 0; i < recs->count; ++i) {
        switch (recs->items[i].severity) {
        case WT_SEVERITY_CRITICAL:
        case WT_SEVERITY_HIGH:   high++;   break;
        case WT_SEVERITY_MEDIUM: medium++; break;
        default:                 low++;    break;
        }
    }

    printf("\nSummary: %zu recommendation(s)", recs->count);
    if (recs->count > 0) {
        printf(" - %zu high, %zu medium, %zu low/info", high, medium, low);
    }
    printf("\n");
    if (high > 0) {
        printf("Start with the high-severity items above.\n");
    } else if (recs->count == 0) {
        printf("Nothing needs attention based on the current samples.\n");
    }
}

int wt_cmd_doctor(const WT_CliOptions *opts)
{
    WT_ScanOptions scan_opts;
    scan_opts.cpu_sample_ms = (opts != NULL && opts->interval_ms > 0)
                                  ? (unsigned int)opts->interval_ms
                                  : 0;
    scan_opts.top_limit = 10;

    WT_ScanReport report;
    WT_Result r = wt_run_scan(&scan_opts, &report);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: scan failed (%s)\n", wt_result_to_string(r));
        return 1;
    }

    WT_RecommendationList recs;
    wt_generate_recommendations(&report, &recs);

    if (opts != NULL && opts->json) {
        wt_print_scan_report_json(&report, &recs, stdout);
        return 0;
    }

    wt_print_scan_report_text(&report, &recs);
    wt_print_doctor_summary(&recs);
    return 0;
}
