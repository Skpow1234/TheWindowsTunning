#include "cli/commands_startup.h"
#include "cli/cli.h"
#include "system/startup.h"
#include "system/file_identity.h"
#include "system/services.h"
#include "system/boot.h"
#include "actions/safe_actions.h"
#include "actions/delay_plan.h"
#include "system/tasks.h"
#include "output/json.h"
#include "platform/service_client.h"
#include "common/units.h"
#include "platform/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

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
        printf("%-8s %-5s %-10s %-11s %ls\n", "Impact", "Score", "Measured",
               "Origin", L"Id / Command");
    } else {
        printf("%-8s %-5s %-11s %ls\n", "Impact", "Score", "Origin",
               L"Id / Command");
    }
    for (size_t i = 0; i < count; ++i) {
        const WT_StartupEntry *e = &entries[i];
        const char *origin = wt_publisher_origin_name(e->identity.origin);
        if (measured_mode && e->measured_available) {
            wchar_t ms[32];
            wt_format_duration_ms(e->measured_ms, ms, 32);
            printf("%-8s %-5d %-10ls %-11s %ls\n",
                   wt_startup_impact_name(e->impact), e->impact_score, ms,
                   origin, e->id);
        } else {
            printf("%-8s %-5d %-11s %ls\n",
                   wt_startup_impact_name(e->impact), e->impact_score, origin,
                   e->id);
        }
        if (e->identity.product_name[0] != L'\0' ||
            e->identity.publisher[0] != L'\0') {
            printf("%-8s   ", "");
            if (e->identity.product_name[0] != L'\0') {
                printf("Product: %.40ls", e->identity.product_name);
                if (e->identity.publisher[0] != L'\0') {
                    printf(" | ");
                }
            }
            if (e->identity.publisher[0] != L'\0') {
                printf("Publisher: %.40ls", e->identity.publisher);
            }
            printf(" (%s)\n", wt_signature_status_name(e->identity.signature));
        } else {
            printf("%-8s   Signature: %s\n", "",
                   wt_signature_status_name(e->identity.signature));
        }
        printf("%-8s   Location: %s%s\n", "",
               wt_install_location_name(e->identity.location),
               e->identity.unusual_location
                   ? " (review — not a malware claim)"
                   : "");
        printf("%-8s   %.88ls\n", "", e->command);
    }
    printf("\nDisable one with: wintune startup disable \"<id>\"\n");
    printf("Delay one with:  wintune startup delay \"<id>\" --seconds 30\n");
    printf("Or plan delays:  wintune startup delay-plan [--include-tasks]\n");
}

static void wt_print_startup_tasks_text(const WT_ScheduledTask *tasks, size_t count,
                                        int measured_mode)
{
    printf("\nScheduled Tasks at Logon/Boot (%zu)\n\n", count);
    if (count == 0) {
        printf("  No logon/boot scheduled tasks found.\n");
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const WT_ScheduledTask *t = &tasks[i];
        if (measured_mode && t->measured_available) {
            wchar_t ms[32];
            wt_format_duration_ms(t->measured_ms, ms, ARRAYSIZE(ms));
            printf("  [%s/%d] %ls  (%s, delay %lus, measured %ls)\n",
                   wt_startup_impact_name(t->impact), t->impact_score, t->id,
                   wt_task_trigger_name(t->trigger_kind),
                   t->delay_seconds, ms);
        } else {
            printf("  [%s/%d] %ls  (%s)\n",
                   wt_startup_impact_name(t->impact), t->impact_score, t->id,
                   wt_task_trigger_name(t->trigger_kind));
        }
        wprintf(L"           %.72ls\n", t->command);
    }
    printf("\nSee full list: wintune tasks list\n");
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

static int wt_startup_delay(const WT_CliOptions *opts)
{
    if (opts->arg2 == NULL) {
        fprintf(stderr,
                "wintune: startup delay requires an entry id\n"
                "Usage: wintune startup delay \"<id>\" --seconds 30\n");
        return 2;
    }
    if (opts->delay_seconds <= 0) {
        fprintf(stderr, "wintune: startup delay requires --seconds <N>\n");
        return 2;
    }

    char msg[512] = {0};
    WT_Result r;
    if (wt_cli_should_route_via_service(opts)) {
        if (!wt_service_client_is_available(2000)) {
            fprintf(stderr, "wintune: WinTune service is not reachable.\n");
            return 1;
        }
        r = wt_service_client_startup_delay(opts->arg2,
                                            (unsigned long)opts->delay_seconds,
                                            opts->yes, msg, sizeof(msg));
    } else {
        r = wt_action_set_startup_delay(opts->arg2,
                                        (unsigned long)opts->delay_seconds,
                                        opts->yes, msg, sizeof(msg));
    }
    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}

static void wt_print_delay_plan_text(const WT_DelayPlan *plan)
{
    printf("WinTune Startup Delay Plan\n\n");
    printf("Base delay: %lu s  |  Stagger: +%lu s per item  |  Cap: %u s\n",
           plan->base_seconds, plan->stagger_seconds, WT_DELAY_PLAN_MAX_SEC);
    printf("Recommended: %zu  |  Protected/excluded: %zu\n\n",
           plan->recommended_count, plan->excluded_count);

    if (plan->count == 0) {
        printf("No delay candidates found. High-impact third-party startups "
               "and (with --include-tasks) logon tasks appear here.\n");
        printf("Microsoft security startups are never auto-delayed.\n");
        return;
    }

    printf("%-8s %-7s %-6s %-5s %ls\n", "Action", "Kind", "Delay", "Score",
           L"Id / Name");
    for (size_t i = 0; i < plan->count; ++i) {
        const WT_DelayPlanItem *it = &plan->items[i];
        if (it->recommended) {
            printf("%-8s %-7s %-6lu %-5d %ls\n", "DELAY",
                   wt_delay_plan_kind_name(it->kind), it->delay_seconds,
                   it->impact_score, it->id);
            wprintf(L"         %ls (%hs)\n", it->name,
                    wt_publisher_origin_name(it->origin));
        } else {
            printf("%-8s %-7s %-6s %-5d %ls\n", "SKIP",
                   wt_delay_plan_kind_name(it->kind), "-", it->impact_score,
                   it->id);
            printf("         %s\n", it->exclude_reason);
        }
    }

    printf("\nPreview only — nothing was changed.\n");
    printf("Apply with:  wintune startup delay-plan apply");
    if (plan->include_tasks) {
        printf(" --include-tasks");
    }
    if (plan->base_seconds != WT_DELAY_PLAN_DEFAULT_BASE_SEC) {
        printf(" --seconds %lu", plan->base_seconds);
    }
    printf("\n");
    printf("Each item confirms unless you pass --yes. Rollback via "
           "'wintune rollback list'.\n");
}

static void wt_print_delay_plan_json(const WT_DelayPlan *plan, FILE *out)
{
    fprintf(out, "{\"command\":\"startup delay-plan\",\"base_seconds\":%lu,"
                 "\"stagger_seconds\":%lu,\"include_tasks\":%s,"
                 "\"recommended_count\":%zu,\"excluded_count\":%zu,\"items\":[",
            plan->base_seconds, plan->stagger_seconds,
            plan->include_tasks ? "true" : "false", plan->recommended_count,
            plan->excluded_count);
    for (size_t i = 0; i < plan->count; ++i) {
        const WT_DelayPlanItem *it = &plan->items[i];
        char id_u8[512];
        char name_u8[256];
        WideCharToMultiByte(CP_UTF8, 0, it->id, -1, id_u8, (int)sizeof(id_u8),
                            NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, it->name, -1, name_u8,
                            (int)sizeof(name_u8), NULL, NULL);
        if (i > 0) {
            fputc(',', out);
        }
        fprintf(out,
                "{\"kind\":\"%s\",\"recommended\":%s,\"excluded\":%s,"
                "\"delay_seconds\":%lu,\"impact_score\":%d,\"origin\":\"%s\","
                "\"id\":\"",
                wt_delay_plan_kind_name(it->kind),
                it->recommended ? "true" : "false",
                it->excluded ? "true" : "false", it->delay_seconds,
                it->impact_score, wt_publisher_origin_name(it->origin));
        for (const char *p = id_u8; *p; ++p) {
            if (*p == '\\' || *p == '"') {
                fputc('\\', out);
            }
            fputc(*p, out);
        }
        fprintf(out, "\",\"name\":\"");
        for (const char *p = name_u8; *p; ++p) {
            if (*p == '\\' || *p == '"') {
                fputc('\\', out);
            }
            fputc(*p, out);
        }
        fprintf(out, "\"");
        if (it->excluded && it->exclude_reason[0] != '\0') {
            fprintf(out, ",\"exclude_reason\":\"%s\"", it->exclude_reason);
        }
        fprintf(out, "}");
    }
    fprintf(out, "]}\n");
}

static int wt_startup_delay_plan(const WT_CliOptions *opts)
{
    unsigned long base = WT_DELAY_PLAN_DEFAULT_BASE_SEC;
    int include_tasks = (opts != NULL && opts->include_tasks);
    int do_apply = 0;
    WT_DelayPlan plan;
    WT_Result r;

    if (opts != NULL && opts->delay_seconds > 0) {
        base = (unsigned long)opts->delay_seconds;
    }
    if (opts != NULL && opts->arg2 != NULL &&
        wcscmp(opts->arg2, L"apply") == 0) {
        do_apply = 1;
    }

    r = wt_delay_plan_build(&plan, base, include_tasks);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: delay-plan failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    if (!do_apply) {
        if (opts != NULL && opts->json) {
            FILE *opened = NULL;
            FILE *out = wt_open_output(opts, &opened);
            if (out == NULL) {
                return 1;
            }
            wt_print_delay_plan_json(&plan, out);
            if (opened != NULL) {
                fclose(opened);
            }
        } else {
            wt_print_delay_plan_text(&plan);
        }
        return 0;
    }

    /* Apply path: show plan first (unless JSON-only automation with --yes). */
    if (opts == NULL || !opts->json) {
        wt_print_delay_plan_text(&plan);
        printf("\n");
    }

    {
        char msg[512];
        r = wt_delay_plan_apply(&plan, opts != NULL && opts->yes, msg,
                                sizeof(msg));
        if (opts != NULL && opts->json) {
            FILE *opened = NULL;
            FILE *out = wt_open_output(opts, &opened);
            if (out == NULL) {
                return 1;
            }
            fprintf(out,
                    "{\"command\":\"startup delay-plan apply\",\"ok\":%s,"
                    "\"message\":\"",
                    (r == WT_OK) ? "true" : "false");
            for (const char *p = msg; *p; ++p) {
                if (*p == '\\' || *p == '"') {
                    fputc('\\', out);
                }
                fputc(*p, out);
            }
            fprintf(out, "\"}\n");
            if (opened != NULL) {
                fclose(opened);
            }
        } else if (msg[0] != '\0') {
            printf("%s\n", msg);
        }
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
        if (wcscmp(opts->arg1, L"delay") == 0) {
            return wt_startup_delay(opts);
        }
        if (wcscmp(opts->arg1, L"delay-plan") == 0) {
            return wt_startup_delay_plan(opts);
        }
        fwprintf(stderr,
                 L"wintune: unknown startup subcommand '%ls'. "
                 L"Use 'disable', 'enable', 'delay', or 'delay-plan'.\n",
                 opts->arg1);
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
            WT_ScheduledTask *tasks = (WT_ScheduledTask *)malloc(
                sizeof(WT_ScheduledTask) * WT_MAX_SCHEDULED_TASKS);
            if (tasks != NULL) {
                size_t task_count = 0;
                if (wt_collect_scheduled_tasks(tasks, WT_MAX_SCHEDULED_TASKS,
                                               &task_count,
                                               WT_TASK_FILTER_STARTUP) == WT_OK) {
                    if (measured_mode) {
                        WT_BootReport boot_tasks;
                        if (wt_collect_boot_from_event_log(&boot_tasks) == WT_OK) {
                            wt_tasks_apply_measured(tasks, task_count, &boot_tasks);
                        }
                    }
                    wt_print_startup_tasks_text(tasks, task_count, measured_mode);
                }
                free(tasks);
            }
        }
    }

    free(entries);
    return rc;
}
