#include "cli/commands_startup.h"
#include "system/startup.h"
#include "system/services.h"
#include "actions/safe_actions.h"
#include "output/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static FILE *wt_open_output(const WT_CliOptions *opts, FILE **opened)
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

static void wt_print_startup_text(const WT_StartupEntry *entries, size_t count)
{
    printf("Startup Entries (%zu)\n\n", count);
    if (count == 0) {
        printf("  No startup entries found.\n");
        return;
    }

    printf("%-8s %ls\n", "Impact", L"Id / Command");
    for (size_t i = 0; i < count; ++i) {
        const WT_StartupEntry *e = &entries[i];
        printf("%-8s %ls\n", wt_startup_impact_name(e->impact), e->id);
        printf("%-8s   %.88ls\n", "", e->command);
    }
    printf("\nDisable one with: wintune startup disable \"<id>\"\n");
}

static void wt_print_auto_services_text(void)
{
    WT_ServiceInfo *svcs =
        (WT_ServiceInfo *)malloc(sizeof(WT_ServiceInfo) * WT_MAX_SERVICES);
    if (svcs == NULL) {
        return;
    }
    size_t count = 0;
    if (wt_collect_services(svcs, WT_MAX_SERVICES, &count) == WT_OK) {
        printf("\nAuto-start Services\n\n");
        printf("%-32.32ls %-8s %ls\n", L"Name", "State", L"Display name");
        for (size_t i = 0; i < count; ++i) {
            if (svcs[i].start_type != WT_SVC_START_AUTO) {
                continue;
            }
            printf("%-32.32ls %-8s %.50ls\n",
                   svcs[i].name,
                   wt_service_state_name(svcs[i].state),
                   svcs[i].display_name);
        }
    }
    free(svcs);
}

static int wt_startup_set_enabled(const WT_CliOptions *opts, int enable)
{
    if (opts->arg2 == NULL) {
        fprintf(stderr,
                "wintune: startup %s requires an entry id\n"
                "Usage: wintune startup %s <id>\n"
                "List ids with 'wintune startup'.\n",
                enable ? "enable" : "disable",
                enable ? "enable" : "disable");
        return 2;
    }
    char msg[512] = {0};
    WT_Result r = wt_action_set_startup_enabled(opts->arg2, enable, opts->yes,
                                                msg, sizeof(msg));
    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}

int wt_cmd_startup(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->arg1 != NULL) {
        if (wcscmp(opts->arg1, L"disable") == 0) {
            return wt_startup_set_enabled(opts, 0);
        }
        if (wcscmp(opts->arg1, L"enable") == 0) {
            return wt_startup_set_enabled(opts, 1);
        }
        fwprintf(stderr,
                 L"wintune: unknown startup subcommand '%ls'. "
                 L"Use 'disable <id>' or 'enable <id>'.\n", opts->arg1);
        return 2;
    }

    WT_StartupEntry *entries =
        (WT_StartupEntry *)malloc(sizeof(WT_StartupEntry) * WT_MAX_STARTUP_ENTRIES);
    if (entries == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    size_t count = 0;
    WT_Result r = wt_collect_startup_entries(entries, WT_MAX_STARTUP_ENTRIES, &count);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: startup scan failed (%s)\n", wt_result_to_string(r));
        free(entries);
        return 1;
    }

    int rc = 0;
    if (opts != NULL && opts->json) {
        FILE *opened = NULL;
        FILE *out = wt_open_output(opts, &opened);
        if (out == NULL) {
            free(entries);
            return 1;
        }
        wt_print_startup_json(entries, count, out);
        if (opened != NULL) {
            fclose(opened);
        }
    } else {
        wt_print_startup_text(entries, count);
        if (opts != NULL && opts->include_services) {
            wt_print_auto_services_text();
        }
        if (opts != NULL && opts->include_tasks) {
            printf("\nScheduled task inspection is not implemented yet "
                   "(planned for a later phase).\n");
        }
    }

    free(entries);
    return rc;
}
