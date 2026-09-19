#include "actions/delay_plan.h"
#include "actions/safe_actions.h"
#include "system/startup.h"
#include "system/tasks.h"
#include "system/boot.h"
#include "common/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <strsafe.h>

void wt_delay_plan_init(WT_DelayPlan *plan)
{
    if (plan == NULL) {
        return;
    }
    memset(plan, 0, sizeof(*plan));
    plan->base_seconds = WT_DELAY_PLAN_DEFAULT_BASE_SEC;
    plan->stagger_seconds = WT_DELAY_PLAN_STAGGER_SEC;
}

const char *wt_delay_plan_kind_name(WT_DelayPlanKind kind)
{
    switch (kind) {
    case WT_DELAY_KIND_STARTUP:
        return "startup";
    case WT_DELAY_KIND_TASK:
        return "task";
    default:
        return "unknown";
    }
}

static int wt_delay_wcs_has_i(const wchar_t *hay, const wchar_t *needle)
{
    wchar_t *h;
    wchar_t *n;
    int found;

    if (hay == NULL || needle == NULL || needle[0] == L'\0') {
        return 0;
    }
    h = _wcsdup(hay);
    n = _wcsdup(needle);
    if (h == NULL || n == NULL) {
        free(h);
        free(n);
        return 0;
    }
    _wcsupr_s(h, wcslen(h) + 1);
    _wcsupr_s(n, wcslen(n) + 1);
    found = (wcsstr(h, n) != NULL);
    free(h);
    free(n);
    return found;
}

int wt_delay_plan_is_security_blocked(const wchar_t *name,
                                      const wchar_t *command,
                                      const WT_FileIdentity *identity)
{
    static const wchar_t *const k_blocked[] = {
        L"SecurityHealth",
        L"Windows Defender",
        L"WindowsSecurity",
        L"MsMpEng",
        L"Sense",
        L"SmartScreen",
        L"NisSrv",
        L"WdNisSvc",
        L"WinDefend",
        L"SecurityHealthSystray",
        L"Windows Security",
        NULL
    };
    size_t i;

    for (i = 0; k_blocked[i] != NULL; ++i) {
        if (wt_delay_wcs_has_i(name, k_blocked[i]) ||
            wt_delay_wcs_has_i(command, k_blocked[i])) {
            return 1;
        }
    }

    if (identity != NULL &&
        (identity->signature == WT_SIG_SIGNED_MICROSOFT ||
         identity->origin == WT_ORIGIN_MICROSOFT)) {
        if (wt_delay_wcs_has_i(name, L"Defender") ||
            wt_delay_wcs_has_i(name, L"Security") ||
            wt_delay_wcs_has_i(identity->product_name, L"Defender") ||
            wt_delay_wcs_has_i(identity->product_name, L"Security Health")) {
            return 1;
        }
        if (identity->location == WT_LOC_WINDOWS &&
            (wt_delay_wcs_has_i(command, L"\\System32\\SecurityHealth") ||
             wt_delay_wcs_has_i(command, L"\\System32\\smartscreen"))) {
            return 1;
        }
    }
    return 0;
}

static int wt_delay_startup_supported(WT_StartupSource src)
{
    return src == WT_STARTUP_SRC_HKCU_RUN || src == WT_STARTUP_SRC_HKLM_RUN ||
           src == WT_STARTUP_SRC_USER_FOLDER ||
           src == WT_STARTUP_SRC_COMMON_FOLDER;
}

static int wt_delay_should_recommend_startup(const WT_StartupEntry *e)
{
    if (e == NULL || !e->enabled) {
        return 0;
    }
    if (!wt_delay_startup_supported(e->source)) {
        return 0;
    }
    if (wt_delay_plan_is_security_blocked(e->name, e->command, &e->identity)) {
        return 0;
    }
    /* Prefer third-party / high-impact / measured-slow items. */
    if (e->identity.origin == WT_ORIGIN_THIRD_PARTY) {
        return 1;
    }
    if (e->impact == WT_STARTUP_IMPACT_HIGH ||
        e->impact == WT_STARTUP_IMPACT_MEDIUM) {
        return 1;
    }
    if (e->impact_score >= 40) {
        return 1;
    }
    if (e->measured_available && e->measured_ms >= 2000u) {
        return 1;
    }
    /* Skip quiet Microsoft / unknown-low items by default. */
    return 0;
}

static int wt_delay_should_recommend_task(const WT_ScheduledTask *t)
{
    if (t == NULL || !t->enabled) {
        return 0;
    }
    if (t->is_microsoft) {
        return 0;
    }
    if (t->trigger_kind != WT_TASK_TRIGGER_LOGON &&
        t->trigger_kind != WT_TASK_TRIGGER_BOOT) {
        return 0;
    }
    if (wt_delay_plan_is_security_blocked(t->name, t->command, NULL)) {
        return 0;
    }
    if (t->impact == WT_STARTUP_IMPACT_HIGH ||
        t->impact == WT_STARTUP_IMPACT_MEDIUM || t->impact_score >= 40) {
        return 1;
    }
    if (t->measured_available && t->measured_ms >= 2000u) {
        return 1;
    }
    /* Third-party logon tasks are fair candidates even if score is unknown. */
    return 1;
}

static WT_DelayPlanItem *wt_delay_plan_add(WT_DelayPlan *plan)
{
    if (plan == NULL || plan->count >= WT_MAX_DELAY_PLAN_ITEMS) {
        return NULL;
    }
    return &plan->items[plan->count++];
}

static void wt_delay_assign_stagger(WT_DelayPlan *plan)
{
    size_t rec_i = 0;
    for (size_t i = 0; i < plan->count; ++i) {
        WT_DelayPlanItem *it = &plan->items[i];
        if (!it->recommended) {
            it->delay_seconds = 0;
            continue;
        }
        unsigned long d =
            plan->base_seconds + (unsigned long)rec_i * plan->stagger_seconds;
        if (d > WT_DELAY_PLAN_MAX_SEC) {
            d = WT_DELAY_PLAN_MAX_SEC;
        }
        it->delay_seconds = d;
        rec_i++;
    }
}

WT_Result wt_delay_plan_build(WT_DelayPlan *plan,
                              unsigned long base_seconds,
                              int include_tasks)
{
    WT_StartupEntry *entries = NULL;
    size_t count = 0;
    WT_BootReport boot;
    WT_Result r;

    if (plan == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_delay_plan_init(plan);
    plan->base_seconds =
        (base_seconds > 0) ? base_seconds : WT_DELAY_PLAN_DEFAULT_BASE_SEC;
    plan->include_tasks = include_tasks ? 1 : 0;

    entries = (WT_StartupEntry *)calloc(WT_MAX_STARTUP_ENTRIES,
                                        sizeof(WT_StartupEntry));
    if (entries == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    r = wt_collect_startup_entries(entries, WT_MAX_STARTUP_ENTRIES, &count);
    if (r != WT_OK) {
        free(entries);
        return r;
    }

    wt_boot_report_init(&boot);
    if (wt_collect_boot_from_event_log(&boot) == WT_OK) {
        wt_startup_apply_measured(entries, count, &boot);
    }

    for (size_t i = 0; i < count; ++i) {
        const WT_StartupEntry *e = &entries[i];
        int security;
        int recommend;
        WT_DelayPlanItem *it;

        if (!e->enabled) {
            continue;
        }
        if (!wt_delay_startup_supported(e->source)) {
            continue;
        }
        security = wt_delay_plan_is_security_blocked(e->name, e->command,
                                                     &e->identity);
        recommend = !security && wt_delay_should_recommend_startup(e);
        if (!security && !recommend) {
            continue;
        }

        it = wt_delay_plan_add(plan);
        if (it == NULL) {
            break;
        }
        StringCchCopyW(it->id, ARRAYSIZE(it->id), e->id);
        StringCchCopyW(it->name, ARRAYSIZE(it->name), e->name);
        it->kind = WT_DELAY_KIND_STARTUP;
        it->impact_score = e->impact_score;
        it->measured_ms = e->measured_available ? e->measured_ms : 0;
        it->origin = e->identity.origin;

        if (security) {
            it->excluded = 1;
            StringCchCopyA(it->exclude_reason, sizeof(it->exclude_reason),
                           "Microsoft security / health (never auto-delayed)");
            plan->excluded_count++;
        } else {
            it->recommended = 1;
            plan->recommended_count++;
        }
    }
    free(entries);

    if (include_tasks) {
        WT_ScheduledTask *tasks =
            (WT_ScheduledTask *)calloc(WT_MAX_SCHEDULED_TASKS,
                                       sizeof(WT_ScheduledTask));
        size_t tcount = 0;
        if (tasks != NULL &&
            wt_collect_scheduled_tasks(tasks, WT_MAX_SCHEDULED_TASKS, &tcount,
                                       WT_TASK_FILTER_STARTUP) == WT_OK) {
            if (boot.boot_duration_ms > 0) {
                wt_tasks_apply_measured(tasks, tcount, &boot);
            }
            for (size_t i = 0; i < tcount; ++i) {
                const WT_ScheduledTask *t = &tasks[i];
                int security;
                int recommend;
                WT_DelayPlanItem *it;

                if (!t->enabled) {
                    continue;
                }
                security = t->is_microsoft ||
                           wt_delay_plan_is_security_blocked(t->name, t->command,
                                                             NULL);
                recommend = !security && wt_delay_should_recommend_task(t);
                if (!security && !recommend) {
                    continue;
                }

                it = wt_delay_plan_add(plan);
                if (it == NULL) {
                    break;
                }
                StringCchCopyW(it->id, ARRAYSIZE(it->id), t->id);
                StringCchCopyW(it->name, ARRAYSIZE(it->name), t->name);
                it->kind = WT_DELAY_KIND_TASK;
                it->impact_score = t->impact_score;
                it->measured_ms = t->measured_available ? t->measured_ms : 0;
                it->origin = t->is_microsoft ? WT_ORIGIN_MICROSOFT
                                             : WT_ORIGIN_THIRD_PARTY;

                if (security) {
                    it->excluded = 1;
                    StringCchCopyA(it->exclude_reason, sizeof(it->exclude_reason),
                                   "Microsoft / security task (never auto-delayed)");
                    plan->excluded_count++;
                } else {
                    it->recommended = 1;
                    plan->recommended_count++;
                }
            }
        }
        free(tasks);
    }

    wt_delay_assign_stagger(plan);
    return WT_OK;
}

WT_Result wt_delay_plan_apply(const WT_DelayPlan *plan,
                              int assume_yes,
                              char *msg,
                              size_t msg_cap)
{
    size_t applied = 0;
    size_t skipped = 0;
    size_t failed = 0;
    char line[384];

    if (msg != NULL && msg_cap > 0) {
        msg[0] = '\0';
    }
    if (plan == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (plan->recommended_count == 0) {
        StringCchCopyA(msg, msg_cap,
                       "No recommended delay candidates. Nothing to apply.");
        return WT_OK;
    }

    if (!assume_yes) {
        char prompt[256];
        StringCchPrintfA(prompt, sizeof(prompt),
                         "Apply staggered delay plan to %zu item(s)? "
                         "(each item will still ask for confirmation)",
                         plan->recommended_count);
        if (!wt_action_confirm(prompt, 0)) {
            StringCchCopyA(msg, msg_cap, "Cancelled. No delays applied.");
            return WT_ERR_CANCELLED;
        }
    }

    for (size_t i = 0; i < plan->count; ++i) {
        const WT_DelayPlanItem *it = &plan->items[i];
        WT_Result r;
        char item_msg[256];

        if (!it->recommended || it->delay_seconds == 0) {
            continue;
        }
        if (wt_delay_plan_is_security_blocked(it->name, NULL, NULL)) {
            skipped++;
            continue;
        }

        item_msg[0] = '\0';
        if (it->kind == WT_DELAY_KIND_STARTUP) {
            r = wt_action_set_startup_delay(it->id, it->delay_seconds,
                                            assume_yes ? 1 : 0, item_msg,
                                            sizeof(item_msg));
        } else {
            r = wt_action_set_task_delay(it->id, it->delay_seconds,
                                         assume_yes ? 1 : 0, item_msg,
                                         sizeof(item_msg));
        }

        if (r == WT_OK) {
            applied++;
        } else if (r == WT_ERR_CANCELLED) {
            skipped++;
        } else {
            failed++;
            fprintf(stderr, "wintune: delay failed for %ls: %s\n", it->id,
                    item_msg[0] != '\0' ? item_msg : wt_result_to_string(r));
        }
    }

    StringCchPrintfA(line, sizeof(line),
                     "Delay plan finished: %zu applied, %zu skipped, %zu failed. "
                     "Use 'wintune rollback list' to undo.",
                     applied, skipped, failed);
    StringCchCopyA(msg, msg_cap, line);
    if (applied == 0 && failed > 0) {
        return WT_ERR_WIN32;
    }
    if (applied == 0 && skipped > 0) {
        return WT_ERR_CANCELLED;
    }
    return WT_OK;
}
