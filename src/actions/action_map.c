#include "actions/action_map.h"

#include "actions/safe_actions.h"

#include <windows.h>
#include <strsafe.h>

static const WT_ApplySpec g_apply_specs[] = {
    { L"WT-POWER-001", WT_APPLY_POWER_SCHEME, WT_POWER_HIGH_PERF, 0 },
    { L"WT-POWER-002", WT_APPLY_POWER_SCHEME, WT_POWER_BALANCED, 0 },
    { L"WT-STARTUP-DISABLE", WT_APPLY_STARTUP_DISABLE, WT_POWER_UNKNOWN, 0 },
    { L"WT-STARTUP-DELAY", WT_APPLY_STARTUP_DELAY, WT_POWER_UNKNOWN, 30 },
    { L"WT-TASK-DISABLE", WT_APPLY_TASK_DISABLE, WT_POWER_UNKNOWN, 0 },
    { L"WT-TASK-DELAY", WT_APPLY_TASK_DELAY, WT_POWER_UNKNOWN, 30 },
};

static const wchar_t *g_advisory_ids[] = {
    L"WT-MEMORY-001",
    L"WT-DISK-001",
    L"WT-DISK-002",
    L"WT-CPU-001",
    L"WT-BOOT-001",
    L"WT-BOOT-002",
    L"WT-STARTUP-001",
    L"WT-UPDATE-001",
    L"WT-UPDATE-002",
    L"WT-UPDATE-003",
    L"WT-UPDATE-004",
    L"WT-BLOCKER-001",
    L"WT-STARTUP-002",
    L"WT-TASK-001",
};

const WT_ApplySpec *wt_apply_spec_lookup(const wchar_t *rec_id)
{
    if (rec_id == NULL || rec_id[0] == L'\0') {
        return NULL;
    }
    for (size_t i = 0; i < ARRAYSIZE(g_apply_specs); ++i) {
        if (_wcsicmp(rec_id, g_apply_specs[i].rec_id) == 0) {
            return &g_apply_specs[i];
        }
    }
    return NULL;
}

int wt_apply_is_advisory_rec_id(const wchar_t *rec_id)
{
    if (rec_id == NULL) {
        return 0;
    }
    for (size_t i = 0; i < ARRAYSIZE(g_advisory_ids); ++i) {
        if (_wcsicmp(rec_id, g_advisory_ids[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

size_t wt_apply_spec_count(void)
{
    return ARRAYSIZE(g_apply_specs);
}

const WT_ApplySpec *wt_apply_spec_at(size_t index)
{
    if (index >= ARRAYSIZE(g_apply_specs)) {
        return NULL;
    }
    return &g_apply_specs[index];
}

WT_Result wt_apply_from_request(const WT_ApplyRequest *req)
{
    if (req == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char *msg = req->msg;
    size_t msg_cap = req->msg_cap;
    if (msg != NULL && msg_cap > 0) {
        msg[0] = '\0';
    }

    if (req->rec_id == NULL || req->rec_id[0] == L'\0') {
        StringCchPrintfA(msg, msg_cap,
                         "Usage: wintune apply <id> [target-id]  "
                         "(e.g. WT-POWER-001 or WT-STARTUP-DISABLE <id>)");
        return WT_ERR_INVALID_ARGUMENT;
    }

    const WT_ApplySpec *spec = wt_apply_spec_lookup(req->rec_id);
    if (spec != NULL) {
        switch (spec->kind) {
        case WT_APPLY_POWER_SCHEME:
            return wt_action_set_power_plan(spec->power_scheme, req->assume_yes,
                                          msg, msg_cap);
        case WT_APPLY_STARTUP_DISABLE:
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchPrintfA(msg, msg_cap,
                                 "'%ls' requires a startup entry id.\n"
                                 "Usage: wintune apply WT-STARTUP-DISABLE "
                                 "<startup-id>",
                                 req->rec_id);
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_action_set_startup_enabled(req->target_id, 0,
                                                 req->assume_yes, msg, msg_cap);
        case WT_APPLY_STARTUP_DELAY: {
            unsigned long delay = req->delay_seconds;
            if (delay == 0) {
                delay = spec->default_delay_seconds;
            }
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchPrintfA(msg, msg_cap,
                                 "'%ls' requires a startup entry id.\n"
                                 "Usage: wintune apply WT-STARTUP-DELAY "
                                 "<startup-id> --seconds 30",
                                 req->rec_id);
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_action_set_startup_delay(req->target_id, delay,
                                               req->assume_yes, msg, msg_cap);
        }
        case WT_APPLY_TASK_DISABLE:
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchPrintfA(msg, msg_cap,
                                 "'%ls' requires a task id.\n"
                                 "Usage: wintune apply WT-TASK-DISABLE <task-id>",
                                 req->rec_id);
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_action_set_task_enabled(req->target_id, 0, req->assume_yes,
                                              msg, msg_cap);
        case WT_APPLY_TASK_DELAY: {
            unsigned long delay = req->delay_seconds;
            if (delay == 0) {
                delay = spec->default_delay_seconds;
            }
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchPrintfA(msg, msg_cap,
                                 "'%ls' requires a task id.\n"
                                 "Usage: wintune apply WT-TASK-DELAY "
                                 "<task-id> --seconds 30",
                                 req->rec_id);
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_action_set_task_delay(req->target_id, delay, req->assume_yes,
                                            msg, msg_cap);
        }
        default:
            break;
        }
    }

    if (wt_apply_is_advisory_rec_id(req->rec_id)) {
        StringCchPrintfA(msg, msg_cap,
                         "'%ls' is advisory: it has no automatic action. "
                         "Follow the steps shown by 'wintune recommend'.",
                         req->rec_id);
        return WT_ERR_NOT_SUPPORTED;
    }

    StringCchPrintfA(msg, msg_cap,
                     "'%ls' is not a recognized recommendation id. "
                     "Run 'wintune recommend' to see current ids.",
                     req->rec_id);
    return WT_ERR_NOT_FOUND;
}
