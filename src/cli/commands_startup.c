#include "cli/commands_startup.h"
#include "cli/cli.h"
#include "system/startup.h"
#include "system/services.h"
#include "system/boot.h"
#include "actions/safe_actions.h"
#include "output/json.h"
#include "platform/service_client.h"
#include "common/units.h"
#include "platform/time.h"

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

static void wt_print_startup_text(const WT_StartupEntry *entries, size_t count,
                                  int measured_mode)
{
    printf("Startup Entries (%zu)%s\n\n", count,
           measured_mode ? " — measured impact from last boot" : "");
    if (count == 0) {
        printf("  No startup entries found.\n");
        return;
    }

    if (measured_mode) {
        printf("%-8s %-10s %ls\n", "Impact", "Measured", L"Id / Command");
    } else {
        printf("%-8s %ls\n", "Impact", L"Id / Command");
    }
    for (size_t i = 0; i < count; ++i) {
        const WT_StartupEntry *e = &entries[i];
        if (measured_mode && e->measured_available) {
            wchar_t ms[32];
            wt_format_duration_ms(e->measured_ms, ms, 32);
            printf("%-8s %-10ls %ls\n",
                   wt_startup_impact_name(e->impact), ms, e->id);
        } else {
            printf("%-8s %ls\n", wt_startup_impact_name(e->impact), e->id);
        }
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
    WT_Result r;
    if (wt_cli_should_route_via_service(opts)) {
        if (!wt_service_client_is_available(2000)) {
            fprintf(stderr,
                    "wintune: WinTune service is not reachable.\n");
            return 1;
        }
        r = wt_service_client_startup_set(opts->arg2, enable, opts->yes, msg,
                                          sizeof(msg));
    } else {
        r = wt_action_set_startup_enabled(opts->arg2, enable, opts->yes, msg,
                                          sizeof(msg));
    }
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

    WT_BootReport boot;
    int measured_mode = (opts != NULL && opts->measured);
    if (measured_mode) {
        WT_Result br = wt_collect_boot_from_event_log(&boot);
        if (br == WT_OK) {
            wt_startup_apply_measured(entries, count, &boot);
        } else if (!opts->json) {
            fprintf(stderr,
                    "wintune: measured startup data unavailable (%s); "
                    "showing heuristic impact only.\n",
                    wt_result_to_string(br));
        }
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
        wt_print_startup_text(entries, count, measured_mode);
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
