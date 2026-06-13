#include "cli/commands_report.h"
#include "cli/cli.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "output/text_report.h"
#include "output/json.h"

#include <stdio.h>
#include <wchar.h>

static int wt_report_use_json(const WT_CliOptions *opts)
{
    if (opts == NULL) {
        return 0;
    }
    if (opts->format != NULL) {
        if (_wcsicmp(opts->format, L"json") == 0) {
            return 1;
        }
        if (_wcsicmp(opts->format, L"text") == 0) {
            return 0;
        }
        fwprintf(stderr,
                 L"wintune: unknown report format '%ls' (use text or json).\n",
                 opts->format);
        return -1;
    }
    return wt_cli_is_json_mode(opts);
}

static FILE *wt_report_open_output(const WT_CliOptions *opts, FILE **opened)
{
    *opened = NULL;
    if (opts != NULL && opts->output_path != NULL) {
        FILE *f = NULL;
        if (_wfopen_s(&f, opts->output_path, L"wb") != 0 || f == NULL) {
            fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                     opts->output_path);
            return NULL;
        }
        *opened = f;
        return f;
    }
    return stdout;
}

int wt_cmd_report(const WT_CliOptions *opts)
{
    int json_mode = wt_report_use_json(opts);
    if (json_mode < 0) {
        return 2;
    }

    WT_ScanOptions scan_opts;
    scan_opts.cpu_sample_ms =
        (opts != NULL && opts->interval_ms > 0) ? (unsigned int)opts->interval_ms : 0;
    scan_opts.sample_count =
        (opts != NULL && opts->samples > 0) ? (unsigned int)opts->samples : 0;
    scan_opts.sample_interval_ms =
        (opts != NULL && opts->interval_ms > 0) ? (unsigned int)opts->interval_ms : 0;
    scan_opts.top_limit = 15;

    WT_ScanReport report;
    WT_Result r = wt_run_scan(&scan_opts, &report);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: scan failed (%s)\n", wt_result_to_string(r));
        return 1;
    }

    WT_RecommendationList recs;
    wt_generate_recommendations(&report, &recs);

    FILE *opened = NULL;
    FILE *out = wt_report_open_output(opts, &opened);
    if (out == NULL) {
        return 1;
    }

    if (json_mode) {
        wt_print_scan_report_json(&report, &recs, out);
    } else {
        wt_print_performance_report_text(out, &report, &recs);
    }

    if (opened != NULL && opts->output_path != NULL &&
            !wt_cli_is_json_mode(opts)) {
        fwprintf(stderr, L"wintune: report written to '%ls'\n", opts->output_path);
    }
    if (opened != NULL) {
        fclose(opened);
    }

    return 0;
}
