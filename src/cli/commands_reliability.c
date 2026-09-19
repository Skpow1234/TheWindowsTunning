#include "cli/commands_reliability.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "common/error.h"
#include "output/json.h"
#include "system/reliability.h"

#include <stdio.h>

static void wt_print_reliability_text(const WT_ReliabilityReport *r)
{
    size_t i;

    printf("Reliability signals (read-only, last %d days)\n\n",
           r->lookback_days);

    printf("Unexpected power / shutdowns\n");
    printf("  Kernel-Power (41):     %u\n", r->kernel_power_count);
    printf("  Unexpected shutdown:   %u\n", r->unexpected_shutdown_count);
    if (r->last_unexpected_utc[0] != '\0') {
        printf("  Most recent:           %s\n", r->last_unexpected_utc);
    }

    printf("\nBugcheck / kernel WER\n");
    printf("  Bugcheck reports:      %u\n", r->bugcheck_count);
    if (r->last_bugcheck_utc[0] != '\0') {
        printf("  Most recent:           %s\n", r->last_bugcheck_utc);
        if (r->last_bugcheck_detail[0] != L'\0') {
            wprintf(L"  Detail:                %ls\n", r->last_bugcheck_detail);
        }
    }

    printf("\nApplication crashes / hangs\n");
    printf("  App crashes (1000):    %u\n", r->app_crash_count);
    printf("  App hangs (1002):      %u\n", r->app_hang_count);
    printf("  WER reports (1001):    %u\n", r->wer_report_count);

    if (r->crash_app_count > 0) {
        printf("\nTop faulting apps\n");
        printf("  %-8s %ls\n", "Count", L"Name");
        for (i = 0; i < r->crash_app_count && i < 8; ++i) {
            printf("  %-8u %ls\n", r->crash_apps[i].count,
                   r->crash_apps[i].name);
        }
    }

    if (r->recent_count > 0) {
        printf("\nRecent events (newest first, capped)\n");
        for (i = 0; i < r->recent_count && i < 12; ++i) {
            const WT_ReliabilityEvent *e = &r->recent[i];
            printf("  %-20s id=%u  %s  ",
                   wt_reliability_kind_name(e->kind), e->event_id,
                   e->time_utc[0] != '\0' ? e->time_utc : "-");
            wprintf(L"%ls\n", e->detail);
        }
    }

    if (r->dump_meta_count > 0) {
        printf("\nLocal dump / WER metadata (names only — contents not read)\n");
        for (i = 0; i < r->dump_meta_count; ++i) {
            const WT_ReliabilityDumpMeta *d = &r->dumps[i];
            printf("  [%ls] ", d->location);
            wprintf(L"%ls", d->name);
            if (d->is_directory) {
                printf("  (dir)");
            } else {
                printf("  %llu bytes", d->size_bytes);
            }
            if (d->modified_utc[0] != '\0') {
                printf("  %s", d->modified_utc);
            }
            printf("\n");
        }
    } else {
        printf("\nNo local minidump / MEMORY.DMP / WER folders listed.\n");
    }

    if (r->note[0] != L'\0') {
        wprintf(L"\n%ls\n", r->note);
    }

    printf("\nWinTune never opens dump contents, never uploads dumps, and "
           "never claims to repair corruption.\n");
    printf("Review Event Viewer or vendor tools if crashes continue.\n");
}

int wt_cmd_reliability(const WT_CliOptions *opts)
{
    WT_ReliabilityReport report;
    WT_Result r = wt_collect_reliability(&report);

    if (r != WT_OK && !report.events_ok && report.dump_meta_count == 0) {
        const char *msg = "Could not collect reliability signals.";
        if (r == WT_ERR_ACCESS_DENIED) {
            msg = "Access denied reading Event Log. Try an elevated shell.";
        }
        return wt_cli_exit_from_result(opts, r, L"reliability", msg);
    }

    if (opts != NULL && opts->json) {
        wt_cli_configure_json_output(opts);
        wt_print_reliability_json(&report, stdout);
        return WT_EXIT_OK;
    }

    wt_print_reliability_text(&report);
    return WT_EXIT_OK;
}
