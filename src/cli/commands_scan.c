#include "cli/commands_scan.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "output/text.h"
#include "output/json.h"
#include "platform/service_client.h"
#include "cli/cli.h"
#include "cli/cli_exit.h"
#include "cli/exit_codes.h"

#include <stdio.h>

int wt_cmd_scan(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->via_service) {
        if (!wt_service_client_is_available(2000)) {
            return wt_cli_exit_from_result(
                opts, WT_ERR_NOT_FOUND, L"scan",
                "WinTune service is not reachable.");
        }

        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 ||
                    opened == NULL) {
                return wt_cli_exit_from_result(
                    opts, WT_ERR_WIN32, L"scan",
                    "Could not open output file.");
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
            return wt_cli_exit_from_result(opts, r, L"scan",
                                           "Service scan failed.");
        }
        return WT_EXIT_OK;
    }

    WT_ScanOptions scan_opts = {0};
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
        return wt_cli_exit_from_result(opts, r, L"scan", "Scan failed.");
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
                return wt_cli_exit_from_result(
                    opts, WT_ERR_WIN32, L"scan",
                    "Could not open output file.");
            }
            out = opened;
        }

        wt_cli_configure_json_output(opts);
        wt_print_scan_report_json(&report, recs_ptr, out);

        if (opened != NULL) {
            fclose(opened);
        }
        return WT_EXIT_OK;
    }

    wt_print_scan_report_text(&report, recs_ptr);
    return WT_EXIT_OK;
}
