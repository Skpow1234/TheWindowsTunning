#include "cli/commands_maintenance.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "common/error.h"
#include "core/scan.h"
#include "output/json.h"
#include "system/maintenance.h"

#include <stdio.h>
#include <string.h>

static void wt_print_maintenance_text(const WT_MaintenanceReport *r)
{
    size_t i;

    printf("Maintenance window overlap (read-only)\n\n");

    printf("Sample pressure\n");
    printf("  Samples:     %u\n", r->sample_count);
    if (r->scan_ok) {
        printf("  CPU:         %.1f%%  (hot samples %u) %s\n",
               r->cpu_percent, r->cpu_hot_samples,
               r->cpu_hot ? "[HOT]" : "");
        printf("  Disk active: %.1f%%  (hot samples %u) %s\n",
               r->disk_active_percent, r->disk_hot_samples,
               r->disk_hot ? "[HOT]" : "");
    } else {
        printf("  (scan metrics unavailable)\n");
    }

    printf("\nMaintenance signals\n");
    printf("  Defender:       %s\n", r->defender_active ? "yes" : "no");
    printf("  Windows Update: %s\n", r->update_active ? "yes" : "no");
    printf("  Optimization:   %s\n", r->optimize_active ? "yes" : "no");
    printf("  Overlap:        %s\n", r->overlap ? "YES" : "no");

    if (r->hit_count > 0) {
        printf("\nHits\n");
        printf("  %-12s %-8s %s\n", "Kind", "Source", "Name");
        for (i = 0; i < r->hit_count; ++i) {
            const WT_MaintHit *h = &r->hits[i];
            printf("  %-12s ", wt_maint_kind_name(h->kind));
            wprintf(L"%-8ls ", h->source);
            wprintf(L"%ls", h->name);
            if (h->task_running) {
                printf(" [running]");
            }
            if (h->last_run_recent && h->last_run_utc[0] != '\0') {
                printf(" last=%s", h->last_run_utc);
            }
            if (h->cpu_percent >= 0.0) {
                printf(" cpu=%.1f%%", h->cpu_percent);
            }
            printf("\n");
        }
    } else {
        printf("\nNo Defender/WU/optimization hits in this window.\n");
    }

    if (r->note[0] != L'\0') {
        wprintf(L"\n%ls\n", r->note);
    }

    printf("\nWinTune never disables Microsoft Defender or Windows Update.\n");
    printf("Use Windows Settings (Active hours / scan schedule) to move "
           "maintenance off work hours.\n");
}

int wt_cmd_maintenance(const WT_CliOptions *opts)
{
    WT_MaintenanceReport report;
    WT_ScanOptions scan_opts;
    WT_ScanReport scan;
    unsigned samples = 3;
    unsigned interval = 1000;
    WT_Result r;

    if (opts != NULL && opts->samples > 0) {
        samples = (unsigned)opts->samples;
    }
    if (opts != NULL && opts->interval_ms > 0) {
        interval = (unsigned)opts->interval_ms;
    }

    memset(&scan_opts, 0, sizeof(scan_opts));
    scan_opts.sample_count = samples;
    scan_opts.sample_interval_ms = interval;
    scan_opts.cpu_sample_ms = interval;
    scan_opts.top_limit = 25;

    r = wt_run_scan(&scan_opts, &scan);
    if (r != WT_OK) {
        wt_maintenance_init(&report);
        (void)wt_maintenance_from_scan(&report, NULL);
        if (report.hit_count == 0) {
            return wt_cli_exit_from_result(
                opts, r, L"maintenance",
                "Could not collect maintenance overlap signals.");
        }
    } else {
        (void)wt_maintenance_from_scan(&report, &scan);
    }

    if (opts != NULL && opts->json) {
        wt_cli_configure_json_output(opts);
        wt_print_maintenance_json(&report, stdout);
        return WT_EXIT_OK;
    }

    wt_print_maintenance_text(&report);
    return WT_EXIT_OK;
}
