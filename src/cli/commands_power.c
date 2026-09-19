#include "cli/commands_power.h"
#include "cli/cli.h"
#include "cli/cli_exit.h"
#include "system/power.h"
#include "actions/apply_preview.h"
#include "actions/safe_actions.h"
#include "platform/service_client.h"

#include <stdio.h>
#include <wchar.h>

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
            printf("  \"battery_percent\": %d,\n", p->battery_percent);
        } else {
            printf("  \"battery_percent\": null,\n");
        }
        if (p->battery_present >= 0) {
            printf("  \"battery_present\": %s,\n",
                   p->battery_present ? "true" : "false");
        } else {
            printf("  \"battery_present\": null,\n");
        }
        if (p->rate_ok) {
            printf("  \"rate_mw\": %d,\n", p->rate_mw);
        } else {
            printf("  \"rate_mw\": null,\n");
        }
        if (p->estimated_seconds >= 0) {
            printf("  \"estimated_seconds\": %d,\n", p->estimated_seconds);
        } else {
            printf("  \"estimated_seconds\": null,\n");
        }
        printf("  \"processor_capped\": %s,\n",
               p->processor_capped ? "true" : "false");
        if (p->processor_max_pct_ac >= 0) {
            printf("  \"processor_max_pct_ac\": %d,\n", p->processor_max_pct_ac);
        } else {
            printf("  \"processor_max_pct_ac\": null,\n");
        }
        if (p->processor_max_pct_dc >= 0) {
            printf("  \"processor_max_pct_dc\": %d\n", p->processor_max_pct_dc);
        } else {
            printf("  \"processor_max_pct_dc\": null\n");
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
    if (p->charging == 1) {
        printf("  Battery state: charging\n");
    } else if (p->discharging == 1) {
        printf("  Battery state: discharging\n");
    }
    if (p->rate_ok && p->on_ac == 0 && p->discharging == 1 && p->rate_mw < 0) {
        printf("  Discharge rate: %.1f W\n", (-(double)p->rate_mw) / 1000.0);
        if (p->estimated_seconds > 0) {
            printf("  Est. remaining: ~%d min\n", p->estimated_seconds / 60);
        }
    }
    if (p->processor_max_pct_ac >= 0 || p->processor_max_pct_dc >= 0) {
        printf("  Processor max: AC %s",
               p->processor_max_pct_ac >= 0 ? "" : "n/a");
        if (p->processor_max_pct_ac >= 0) {
            printf("%d%%", p->processor_max_pct_ac);
        }
        printf(" / DC ");
        if (p->processor_max_pct_dc >= 0) {
            printf("%d%%", p->processor_max_pct_dc);
        } else {
            printf("n/a");
        }
        if (p->processor_capped) {
            printf(" (capped for current source)");
        }
        printf("\n");
    }

    const char *action_id = NULL;
    const char *rec = wt_power_recommend(p, &action_id);
    if (rec != NULL) {
        const char *set_token = (p->on_ac == 1) ? "performance" : "balanced";
        printf("\nRecommendation:\n  %s\n", rec);
        printf("\nSafe action:\n  [%s] wintune power --set %s\n",
               action_id, set_token);
    } else if (p->processor_capped) {
        printf("\nNote: the active plan caps processor maximum state for this "
               "power source.\n");
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

        if (opts->dry_run) {
            WT_ApplyPreview preview;
            r = wt_apply_preview_power(target, &preview);
            if (wt_cli_is_json_mode(opts)) {
                wt_apply_preview_print_json(stdout, &preview);
            } else {
                wt_apply_preview_print_text(stdout, &preview);
            }
            return wt_cli_exit_from_result(opts, r, L"power",
                                           preview.summary[0] != '\0'
                                               ? preview.summary
                                               : NULL);
        }

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
