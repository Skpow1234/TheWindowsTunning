#include "cli/commands_updates.h"
#include "system/updates.h"
#include "output/json.h"

#include <stdio.h>

static void wt_print_reboot_reasons(const WT_UpdateStatus *s)
{
    if (!s->reboot_required) {
        printf("  Reboot required:  no\n");
        return;
    }

    printf("  Reboot required:  yes\n");
    if (s->reboot_wu) {
        printf("    - Windows Update pending reboot\n");
    }
    if (s->reboot_cbs) {
        printf("    - Component Based Servicing (CBS) pending reboot\n");
    }
    if (s->reboot_pending_file_rename) {
        printf("    - Pending file rename operations\n");
    }
}

static void wt_print_updates_text(const WT_UpdateStatus *s)
{
    printf("Windows Update\n\n");

    wt_print_reboot_reasons(s);

    if (s->wu_service_running >= 0) {
        printf("  WU service:       %s\n",
               s->wu_service_running ? "running" : "stopped");
    }

    char ts[32];
    if (s->last_check_available &&
        wt_format_filetime_iso8601_utc(&s->last_check_utc, ts, sizeof(ts)) == WT_OK) {
        printf("  Last check:       %s\n", ts);
    } else {
        printf("  Last check:       (unknown)\n");
    }

    if (s->last_install_available &&
        wt_format_filetime_iso8601_utc(&s->last_install_utc, ts, sizeof(ts)) == WT_OK) {
        printf("  Last install:     %s", ts);
        if (s->last_install_title[0] != L'\0') {
            wprintf(L"  (%ls)", s->last_install_title);
        }
        printf("\n");
    } else if (s->last_install_title[0] != L'\0') {
        wprintf(L"  Last install:     %ls\n", s->last_install_title);
    } else {
        printf("  Last install:     (unknown)\n");
    }

    if (s->search_available) {
        printf("\nPending updates:  %lu", s->pending_count);
        if (s->pending_mandatory_count > 0) {
            printf(" (%lu mandatory)", s->pending_mandatory_count);
        }
        if (s->pending_reboot_count > 0) {
            printf(" (%lu require reboot)", s->pending_reboot_count);
        }
        printf("\n");

        if (s->pending_count == 0) {
            printf("  No pending updates reported by Windows Update.\n");
        } else {
            printf("\n%-4s %-4s %-6s %ls\n", "Mand", "DL", "Reboot", L"Title");
            for (unsigned long i = 0; i < s->pending_count; ++i) {
                const WT_PendingUpdate *p = &s->pending[i];
                wprintf(L"%-4s %-4s %-6s %.72ls\n",
                        p->mandatory ? "yes" : "no",
                        p->downloaded ? "yes" : "no",
                        p->reboot_required ? "yes" : "no",
                        p->title);
            }
        }
    } else if (s->note[0] != L'\0') {
        wprintf(L"\nNote: %ls\n", s->note);
    }

    printf("\nWinTune reports update state only; it never installs updates.\n");
    if (s->reboot_required) {
        printf("Recommendation: schedule a reboot when convenient.\n");
    }
}

int wt_cmd_updates(const WT_CliOptions *opts)
{
    WT_UpdateStatus status;
    WT_Result r = wt_collect_update_status(&status);
    if (r != WT_OK && !status.reboot_required && status.pending_count == 0) {
        fprintf(stderr, "wintune: update status failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    if (opts != NULL && opts->json) {
        wt_print_updates_json(&status, stdout);
        return 0;
    }

    wt_print_updates_text(&status);
    return 0;
}
