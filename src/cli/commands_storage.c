#include "cli/commands_storage.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "common/error.h"
#include "output/json.h"
#include "system/storage_health.h"

#include <stdio.h>

static void wt_print_storage_text(const WT_StorageHealthReport *r)
{
    size_t i;

    printf("Storage health (read-only)\n\n");

    if (r->disk_count == 0) {
        printf("  No physical disks reported.\n");
    } else {
        printf("%-6s %-8s %-6s %-8s %-36s %ls\n",
               "Drive", "Status", "Fail?", "Media", "Bus", L"Model");
        for (i = 0; i < r->disk_count; ++i) {
            const WT_StorageDiskHealth *d = &r->disks[i];
            const char *fail = "?";
            if (d->predict_failure == 1) {
                fail = "yes";
            } else if (d->predict_failure == 0) {
                fail = "no";
            }
            printf("%-6u %-8s %-6s %-8ls %-36ls ",
                   d->physical_drive,
                   wt_storage_health_status_name(d->status),
                   fail,
                   d->media_hint,
                   d->bus_type);
            wprintf(L"%.48ls\n", d->model);
            if (d->serial[0] != L'\0') {
                wprintf(L"         serial: %ls\n", d->serial);
            }
            if (d->note[0] != L'\0' &&
                d->status != WT_STORAGE_HEALTH_OK) {
                wprintf(L"         note: %ls\n", d->note);
            }
        }
    }

    if (r->note[0] != L'\0') {
        wprintf(L"\n%ls\n", r->note);
    }

    printf("\nWinTune never wipes, formats, or repairs disks.\n");
    if (r->any_degraded) {
        printf("If failure is predicted: back up now, then use your disk "
               "vendor's diagnostic tool.\n");
        printf("Also review: wintune scan / wintune doctor\n");
    } else {
        printf("This is a reliability signal only — not a full SMART dump.\n");
    }
}

int wt_cmd_storage(const WT_CliOptions *opts)
{
    WT_StorageHealthReport report;
    WT_Result r = wt_collect_storage_health(&report);

    if (r != WT_OK && report.disk_count == 0) {
        const char *msg = "Could not open any physical disks.";
        if (r == WT_ERR_ACCESS_DENIED) {
            msg = "Access denied opening physical disks. Try an elevated shell.";
        } else if (r == WT_ERR_NOT_FOUND) {
            msg = "No physical disks could be opened.";
        }
        return wt_cli_exit_from_result(opts, r, L"storage", msg);
    }

    if (opts != NULL && opts->json) {
        wt_cli_configure_json_output(opts);
        wt_print_storage_json(&report, stdout);
        return WT_EXIT_OK;
    }

    wt_print_storage_text(&report);
    return WT_EXIT_OK;
}
