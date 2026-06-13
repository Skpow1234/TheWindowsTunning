#include "cli/commands_tasks.h"
#include "cli/cli.h"
#include "system/tasks.h"
#include "system/boot.h"
#include "actions/safe_actions.h"
#include "output/json.h"
#include "platform/service_client.h"
#include "common/units.h"

#include <stdio.h>
#include <stdlib.h>

static WT_TaskCollectFilter wt_tasks_list_filter(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->tasks_logon) {
        return WT_TASK_FILTER_LOGON;
    }
    return WT_TASK_FILTER_STARTUP;
}

static void wt_print_tasks_text(const WT_ScheduledTask *tasks, size_t count,
                                int measured_mode)
{
    printf("Scheduled Tasks (%zu)%s\n\n", count,
           measured_mode ? " — measured impact from last boot" : "");
    if (count == 0) {
        printf("  No matching tasks found.\n");
        return;
    }

    if (measured_mode) {
        printf("%-8s %-8s %-6s %-6s %ls\n",
               "Impact", "Trigger", "Delay", "Meas.", L"Id / Command");
    } else {
        printf("%-8s %-8s %-6s %ls\n",
               "Impact", "Trigger", "Delay", L"Id / Command");
    }

    for (size_t i = 0; i < count; ++i) {
        const WT_ScheduledTask *t = &tasks[i];
        char delay[16];
        if (t->delay_seconds > 0) {
            snprintf(delay, sizeof(delay), "%lus", t->delay_seconds);
        } else {
            snprintf(delay, sizeof(delay), "-");
        }

        if (measured_mode && t->measured_available) {
            wchar_t ms[32];
            wt_format_duration_ms(t->measured_ms, ms, ARRAYSIZE(ms));
            printf("%-8s %-8s %-6s %-6ls %ls\n",
                   wt_startup_impact_name(t->impact),
                   wt_task_trigger_name(t->trigger_kind),
                   delay, ms, t->id);
        } else {
            printf("%-8s %-8s %-6s %ls\n",
                   wt_startup_impact_name(t->impact),
                   wt_task_trigger_name(t->trigger_kind),
                   delay, t->id);
        }
        printf("%-8s   %.88ls\n", "", t->command);
        if (t->is_microsoft) {
            printf("%-8s   (protected Microsoft task)\n", "");
        }
    }

    printf("\nDisable: wintune tasks disable \"<id>\"\n");
    printf("Delay:   wintune tasks delay \"<id>\" --seconds 30\n");
}

static int wt_tasks_list(const WT_CliOptions *opts)
{
    WT_ScheduledTask *tasks =
        (WT_ScheduledTask *)malloc(sizeof(WT_ScheduledTask) * WT_MAX_SCHEDULED_TASKS);
    if (tasks == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }

    size_t count = 0;
    WT_Result r = wt_collect_scheduled_tasks(
        tasks, WT_MAX_SCHEDULED_TASKS, &count, wt_tasks_list_filter(opts));
    if (r != WT_OK) {
        fprintf(stderr, "wintune: task scan failed (%s)\n",
                wt_result_to_string(r));
        free(tasks);
        return 1;
    }

    int measured_mode = (opts != NULL && opts->measured);
    if (measured_mode) {
        WT_BootReport boot;
        if (wt_collect_boot_from_event_log(&boot) == WT_OK) {
            wt_tasks_apply_measured(tasks, count, &boot);
        } else if (opts == NULL || !opts->json) {
            fprintf(stderr,
                    "wintune: measured task data unavailable; "
                    "showing heuristic impact only.\n");
        }
    }

    if (opts != NULL && opts->json) {
        wt_print_tasks_json(tasks, count, stdout);
    } else {
        wt_print_tasks_text(tasks, count, measured_mode);
    }

    free(tasks);
    return 0;
}

static int wt_tasks_set_enabled(const WT_CliOptions *opts, int enable)
{
    if (opts->arg2 == NULL) {
        fprintf(stderr,
                "wintune: tasks %s requires a task id\n"
                "Usage: wintune tasks %s \"<id>\"\n"
                "List ids with 'wintune tasks list'.\n",
                enable ? "enable" : "disable",
                enable ? "enable" : "disable");
        return 2;
    }

    char msg[512] = {0};
    WT_Result r;
    if (wt_cli_should_route_via_service(opts)) {
        if (!wt_service_client_is_available(2000)) {
            fprintf(stderr, "wintune: WinTune service is not reachable.\n");
            return 1;
        }
        r = wt_service_client_task_set(opts->arg2, enable, 0, opts->yes,
                                       msg, sizeof(msg));
    } else {
        r = wt_action_set_task_enabled(opts->arg2, enable, opts->yes,
                                       msg, sizeof(msg));
    }
    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}

static int wt_tasks_delay(const WT_CliOptions *opts)
{
    if (opts->arg2 == NULL) {
        fprintf(stderr,
                "wintune: tasks delay requires a task id\n"
                "Usage: wintune tasks delay \"<id>\" --seconds 30\n");
        return 2;
    }
    if (opts->delay_seconds <= 0) {
        fprintf(stderr,
                "wintune: tasks delay requires --seconds <N>\n");
        return 2;
    }

    char msg[512] = {0};
    WT_Result r;
    if (wt_cli_should_route_via_service(opts)) {
        if (!wt_service_client_is_available(2000)) {
            fprintf(stderr, "wintune: WinTune service is not reachable.\n");
            return 1;
        }
        r = wt_service_client_task_set(opts->arg2, 1,
                                       (unsigned long)opts->delay_seconds,
                                       opts->yes, msg, sizeof(msg));
    } else {
        r = wt_action_set_task_delay(opts->arg2,
                                     (unsigned long)opts->delay_seconds,
                                     opts->yes, msg, sizeof(msg));
    }
    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}

int wt_cmd_tasks(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "Usage:\n"
                "  wintune tasks list [--logon] [--measured]\n"
                "  wintune tasks disable \"<id>\"\n"
                "  wintune tasks enable \"<id>\"\n"
                "  wintune tasks delay \"<id>\" --seconds 30\n");
        return 2;
    }

    if (wcscmp(opts->arg1, L"list") == 0) {
        return wt_tasks_list(opts);
    }
    if (wcscmp(opts->arg1, L"disable") == 0) {
        return wt_tasks_set_enabled(opts, 0);
    }
    if (wcscmp(opts->arg1, L"enable") == 0) {
        return wt_tasks_set_enabled(opts, 1);
    }
    if (wcscmp(opts->arg1, L"delay") == 0) {
        return wt_tasks_delay(opts);
    }

    fwprintf(stderr, L"wintune: unknown tasks subcommand '%ls'.\n", opts->arg1);
    return 2;
}
