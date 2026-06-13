#include "cli/commands_blockers.h"
#include "system/blockers.h"
#include "output/json.h"

#include <stdio.h>

static const wchar_t *wt_blocker_kind_label(WT_BlockerKind kind)
{
    switch (kind) {
    case WT_BLOCKER_SHUTDOWN:  return L"shutdown";
    case WT_BLOCKER_FILE_LOCK: return L"file_lock";
    default:                   return L"unknown";
    }
}

static void wt_print_reboot_context(const WT_BlockerReport *r)
{
    if (!r->reboot_pending) {
        printf("  Reboot pending:   no\n");
        return;
    }

    printf("  Reboot pending:   yes\n");
    if (r->reboot_wu) {
        printf("    - Windows Update pending reboot\n");
    }
    if (r->reboot_cbs) {
        printf("    - Component Based Servicing (CBS) pending reboot\n");
    }
    if (r->reboot_pending_file_rename) {
        printf("    - Pending file rename operations\n");
    }
}

static void wt_print_blockers_text(const WT_BlockerReport *r)
{
    printf("Restart / update blockers\n\n");
    wt_print_reboot_context(r);

    if (r->pending_rename_file_count > 0) {
        printf("\nPending file operations:  %lu path(s) scheduled at reboot\n",
               r->pending_rename_file_count);
    }

    printf("\nApplications blocking shutdown\n");
    if (r->process_blocker_count == 0) {
        printf("  (none detected)\n");
    } else {
        printf("\n%-8s %-24s %-12s %ls\n", "PID", "Process", "Kind", L"Reason");
        for (unsigned long i = 0; i < r->process_blocker_count; ++i) {
            const WT_BlockerProcess *b = &r->process_blockers[i];
            printf("%-8lu %-24ls %-12ls ",
                   b->pid, b->name, wt_blocker_kind_label(b->kind));
            if (b->reason[0] != L'\0') {
                wprintf(L"%.80ls\n", b->reason);
            } else if (b->app_name[0] != L'\0') {
                wprintf(L"%ls\n", b->app_name);
            } else {
                printf("-\n");
            }
        }
    }

    printf("\nLocked files (Restart Manager)\n");
    if (r->locked_file_count == 0) {
        printf("  (none detected)\n");
    } else {
        for (unsigned long i = 0; i < r->locked_file_count; ++i) {
            const WT_LockedFile *lf = &r->locked_files[i];
            wprintf(L"\n  %ls\n", lf->path);
            for (unsigned long p = 0; p < lf->process_count; ++p) {
                wprintf(L"    PID %lu  %ls\n", lf->pids[p],
                        lf->process_names[p]);
            }
        }
    }

    if (r->note[0] != L'\0') {
        wprintf(L"\nNote: %ls\n", r->note);
    }

    printf("\nWinTune never closes applications automatically.\n");
    if (r->process_blocker_count > 0 || r->locked_file_count > 0) {
        printf("Close the listed applications manually, then retry reboot or "
               "update installation.\n");
    } else if (r->reboot_pending) {
        printf("No blockers were found; a reboot may still be required to "
               "finish servicing.\n");
    }
}

int wt_cmd_blockers(const WT_CliOptions *opts)
{
    WT_BlockerReport report;
    WT_Result r = wt_collect_blockers(&report);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: blockers collection failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    if (opts != NULL && opts->json) {
        wt_print_blockers_json(&report, stdout);
        return 0;
    }

    wt_print_blockers_text(&report);
    return 0;
}
