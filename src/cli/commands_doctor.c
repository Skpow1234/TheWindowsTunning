#include "cli/commands_doctor.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "system/updates.h"
#include "output/text.h"
#include "output/json.h"
#include "platform/service_client.h"
#include "cli/cli.h"

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
    if (opts != NULL && opts->via_service) {
        if (!wt_service_client_is_available(2000)) {
            fprintf(stderr,
                    "wintune: WinTune service is not reachable.\n"
                    "Install/start it with: wintune service install && "
                    "wintune service start\n");
            return 1;
        }

        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 ||
                    opened == NULL) {
                fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                         opts->output_path);
                return 1;
            }
            out = opened;
        }

        int text_format = !wt_cli_is_json_mode(opts);
        WT_Result r = wt_service_client_scan(
            1, opts->interval_ms > 0 ? opts->interval_ms : 0, text_format, out);
        if (opened != NULL) {
            fclose(opened);
        }
        if (r != WT_OK) {
            fprintf(stderr, "wintune: service doctor failed (%s)\n",
                    wt_result_to_string(r));
            return 1;
        }
        return 0;
    }

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

    if (report.updates_ok) {
        (void)wt_search_pending_updates(&report.updates);
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
