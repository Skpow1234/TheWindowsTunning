#include "cli/commands_power.h"
#include "cli/cli.h"
#include "cli/cli_exit.h"
#include "system/power.h"
#include "actions/safe_actions.h"
#include "platform/service_client.h"

#include <stdio.h>

static void wt_power_source_label(const WT_PowerInfo *p, char *buf, size_t cap)
{
    if (p->on_ac == 1) {
        snprintf(buf, cap, "AC");
    } else if (p->on_ac == 0) {
        snprintf(buf, cap, "Battery");
    } else {
        snprintf(buf, cap, "Unknown");
    }
}

/* Picks a single, conservative recommendation token for the current state, or
 * NULL when the current plan is already appropriate. */
static const char *wt_power_recommend(const WT_PowerInfo *p,
                                      const char **action_id)
{
    *action_id = NULL;
    if (p->on_ac == 1 &&
        p->scheme != WT_POWER_HIGH_PERF && p->scheme != WT_POWER_ULTIMATE) {
        *action_id = "WT-POWER-001";
        return "On AC power: High performance can improve responsiveness for "
               "heavy workloads.";
    }
    if (p->on_ac == 0 &&
        (p->scheme == WT_POWER_HIGH_PERF || p->scheme == WT_POWER_ULTIMATE)) {
        *action_id = "WT-POWER-002";
        return "On battery: a Balanced plan can extend battery life.";
    }
    return NULL;
}

static int wt_power_show(const WT_CliOptions *opts, const WT_PowerInfo *p)
{
    if (opts != NULL && opts->json) {
        printf("{\n");
        printf("  \"scheme\": \"%s\",\n", wt_power_scheme_name(p->scheme));
        printf("  \"active_name\": \"%ls\",\n", p->active_name);
        printf("  \"power_source\": \"%s\",\n",
               (p->on_ac == 1) ? "ac" : (p->on_ac == 0) ? "battery" : "unknown");
        if (p->battery_percent >= 0) {
            printf("  \"battery_percent\": %d\n", p->battery_percent);
        } else {
            printf("  \"battery_percent\": null\n");
        }
        printf("}\n");
        return 0;
    }

    char source[16];
    wt_power_source_label(p, source, sizeof(source));

    printf("Power\n\n");
    printf("  Current plan: %ls (%s)\n", p->active_name, wt_power_scheme_name(p->scheme));
    printf("  Power source: %s", source);
    if (p->battery_percent >= 0) {
        printf("  (battery %d%%)", p->battery_percent);
    }
    printf("\n");

    const char *action_id = NULL;
    const char *rec = wt_power_recommend(p, &action_id);
    if (rec != NULL) {
        const char *set_token = (p->on_ac == 1) ? "performance" : "balanced";
        printf("\nRecommendation:\n  %s\n", rec);
        printf("\nSafe action:\n  [%s] wintune power --set %s\n",
               action_id, set_token);
    } else {
        printf("\nThe current power plan looks appropriate for the power source.\n");
    }
    return 0;
}

int wt_cmd_power(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->set_value != NULL) {
        WT_PowerScheme target = wt_power_scheme_from_token(opts->set_value);
        if (target == WT_POWER_UNKNOWN) {
            fwprintf(stderr,
                     L"wintune: unknown power plan '%ls'. "
                     L"Use balanced | performance | saver | ultimate.\n",
                     opts->set_value);
            return wt_cli_exit_usage(opts, L"power", "unknown power plan");
        }
        char msg[512] = {0};
        WT_Result r;
        if (wt_cli_should_route_via_service(opts)) {
            if (!wt_service_client_is_available(2000)) {
                return wt_cli_exit_from_result(
                    opts, WT_ERR_NOT_FOUND, L"power",
                    "WinTune service is not reachable.");
            }
            r = wt_service_client_power_set(opts->set_value, opts->yes, msg,
                                            sizeof(msg));
        } else {
            r = wt_action_set_power_plan(target, opts->yes, msg, sizeof(msg));
        }
        if (msg[0] != '\0' && !wt_cli_is_json_mode(opts)) {
            printf("%s\n", msg);
        }
        return wt_cli_exit_from_result(opts, r, L"power",
                                       msg[0] != '\0' ? msg : NULL);
    }

    WT_PowerInfo p;
    if (wt_collect_power_info(&p) != WT_OK) {
        return wt_cli_exit_from_result(opts, WT_ERR_WIN32, L"power",
                                       "Could not read power information.");
    }
    return wt_power_show(opts, &p);
}
