#include "cli/commands_scan.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "output/text.h"
#include "output/json.h"
#include "platform/service_client.h"
#include "cli/cli.h"

#include <stdio.h>

int wt_cmd_scan(const WT_CliOptions *opts)
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
            0, opts->interval_ms > 0 ? opts->interval_ms : 0, text_format, out);
        if (opened != NULL) {
            fclose(opened);
        }
        if (r != WT_OK) {
            fprintf(stderr, "wintune: service scan failed (%s)\n",
                    wt_result_to_string(r));
            return 1;
        }
        return 0;
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

    WT_RecommendationList recs;
    int want_recs = (opts == NULL || !opts->no_recommendations);
    if (want_recs) {
        wt_generate_recommendations(&report, &recs);
    }
    const WT_RecommendationList *recs_ptr = want_recs ? &recs : NULL;

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

        wt_print_scan_report_json(&report, recs_ptr, out);

        if (opened != NULL) {
            fclose(opened);
        }
        return 0;
    }

    wt_print_scan_report_text(&report, recs_ptr);
    return 0;
}
