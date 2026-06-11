#include "cli/commands_services.h"
#include "system/services.h"
#include "system/privilege.h"
#include "actions/safe_actions.h"
#include "output/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static int wt_service_matches(const WT_ServiceInfo *s, const WT_CliOptions *o)
{
    int any_filter = o->svc_auto || o->svc_running || o->svc_stopped || o->svc_failed;
    if (!any_filter) {
        return 1;
    }
    if (o->svc_auto && s->start_type == WT_SVC_START_AUTO) {
        return 1;
    }
    if (o->svc_running && s->state == WT_SVC_STATE_RUNNING) {
        return 1;
    }
    if (o->svc_stopped && s->state == WT_SVC_STATE_STOPPED) {
        return 1;
    }
    /* "failed" = configured to auto-start but currently stopped. */
    if (o->svc_failed && s->start_type == WT_SVC_START_AUTO &&
        s->state == WT_SVC_STATE_STOPPED) {
        return 1;
    }
    return 0;
}

/* Compacts the matching services to the front of the array and returns the
 * filtered count. */
static size_t wt_filter_services(WT_ServiceInfo *svcs, size_t count,
                                 const WT_CliOptions *opts)
{
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        if (opts == NULL || wt_service_matches(&svcs[i], opts)) {
            if (kept != i) {
                svcs[kept] = svcs[i];
            }
            kept++;
        }
    }
    return kept;
}

static void wt_print_services_text(const WT_ServiceInfo *svcs, size_t count,
                                   size_t total)
{
    size_t running = 0, stopped = 0, autostart = 0;
    for (size_t i = 0; i < count; ++i) {
        if (svcs[i].state == WT_SVC_STATE_RUNNING) running++;
        if (svcs[i].state == WT_SVC_STATE_STOPPED) stopped++;
        if (svcs[i].start_type == WT_SVC_START_AUTO) autostart++;
    }

    printf("Services (%zu of %zu)\n\n", count, total);
    if (count == 0) {
        printf("  No services matched.\n");
        return;
    }

    printf("%-34.34ls %-9s %-9s %-7s %ls\n",
           L"Name", "State", "Start", "PID", L"Display name");
    for (size_t i = 0; i < count; ++i) {
        const WT_ServiceInfo *s = &svcs[i];
        if (s->pid == 0) {
            printf("%-34.34ls %-9s %-9s %-7s %.44ls\n",
                   s->name,
                   wt_service_state_name(s->state),
                   wt_service_start_type_name(s->start_type),
                   "-",
                   s->display_name);
        } else {
            printf("%-34.34ls %-9s %-9s %-7lu %.44ls\n",
                   s->name,
                   wt_service_state_name(s->state),
                   wt_service_start_type_name(s->start_type),
                   s->pid,
                   s->display_name);
        }
    }

    printf("\nSummary: %zu running, %zu stopped, %zu auto-start\n",
           running, stopped, autostart);
}

static int wt_services_restart(const WT_CliOptions *opts)
{
    if (opts->arg2 == NULL) {
        fprintf(stderr,
                "wintune: services restart requires a service name\n"
                "Usage: wintune services restart <name>\n");
        return 2;
    }
    char msg[512] = {0};
    WT_Result r = wt_action_restart_service(opts->arg2, opts->yes,
                                            msg, sizeof(msg));
    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}

int wt_cmd_services(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->arg1 != NULL) {
        if (wcscmp(opts->arg1, L"restart") == 0) {
            return wt_services_restart(opts);
        }
        fwprintf(stderr,
                 L"wintune: unknown services subcommand '%ls'. "
                 L"Use 'restart <name>'.\n", opts->arg1);
        return 2;
    }

    WT_ServiceInfo *svcs =
        (WT_ServiceInfo *)malloc(sizeof(WT_ServiceInfo) * WT_MAX_SERVICES);
    if (svcs == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    size_t total = 0;
    WT_Result r = wt_collect_services(svcs, WT_MAX_SERVICES, &total);
    if (r != WT_OK) {
        if (r == WT_ERR_ACCESS_DENIED) {
            wt_print_admin_required_message(stderr);
            fprintf(stderr,
                    "wintune: could not open the Service Control Manager.\n");
        } else {
            fprintf(stderr, "wintune: service scan failed (%s)\n",
                    wt_result_to_string(r));
        }
        free(svcs);
        return 1;
    }

    size_t count = wt_filter_services(svcs, total, opts);

    if (opts != NULL && opts->json) {
        FILE *out = stdout;
        FILE *opened = NULL;
        if (opts->output_path != NULL) {
            if (_wfopen_s(&opened, opts->output_path, L"wb") != 0 || opened == NULL) {
                fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                         opts->output_path);
                free(svcs);
                return 1;
            }
            out = opened;
        }
        wt_print_services_json(svcs, count, out);
        if (opened != NULL) {
            fclose(opened);
        }
    } else {
        wt_print_services_text(svcs, count, total);
    }

    free(svcs);
    return 0;
}
