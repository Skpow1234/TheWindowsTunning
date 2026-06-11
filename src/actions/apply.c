#include "actions/apply.h"
#include "actions/safe_actions.h"
#include "system/power.h"

#include <windows.h>
#include <strsafe.h>

typedef struct WT_ApplyActionEntry {
    const wchar_t *id;
    WT_PowerScheme scheme;
} WT_ApplyActionEntry;

static const WT_ApplyActionEntry g_apply_actions[] = {
    { L"WT-POWER-001", WT_POWER_HIGH_PERF },
    { L"WT-POWER-002", WT_POWER_BALANCED },
};

static const wchar_t *g_advisory_ids[] = {
    L"WT-MEMORY-001",
    L"WT-DISK-001",
    L"WT-DISK-002",
    L"WT-CPU-001",
};

static int wt_apply_lookup_power(const wchar_t *id, WT_PowerScheme *out)
{
    for (size_t i = 0; i < ARRAYSIZE(g_apply_actions); ++i) {
        if (_wcsicmp(id, g_apply_actions[i].id) == 0) {
            *out = g_apply_actions[i].scheme;
            return 1;
        }
    }
    return 0;
}

static int wt_apply_is_advisory_id(const wchar_t *id)
{
    for (size_t i = 0; i < ARRAYSIZE(g_advisory_ids); ++i) {
        if (_wcsicmp(id, g_advisory_ids[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

WT_Result wt_apply_recommendation(const wchar_t *id,
                                  int assume_yes,
                                  char *msg, size_t msg_cap)
{
    if (msg != NULL && msg_cap > 0) {
        msg[0] = '\0';
    }
    if (id == NULL || id[0] == L'\0') {
        StringCchPrintfA(msg, msg_cap,
                         "Usage: wintune apply <id>  (e.g. WT-POWER-001)");
        return WT_ERR_INVALID_ARGUMENT;
    }

    WT_PowerScheme target;
    if (wt_apply_lookup_power(id, &target)) {
        return wt_action_set_power_plan(target, assume_yes, msg, msg_cap);
    }

    if (wt_apply_is_advisory_id(id)) {
        StringCchPrintfA(msg, msg_cap,
                         "'%ls' is advisory: it has no automatic action. "
                         "Follow the suggested steps from 'wintune recommend'.",
                         id);
        return WT_ERR_NOT_SUPPORTED;
    }

    StringCchPrintfA(msg, msg_cap,
                     "'%ls' is not a recognized recommendation id. "
                     "Run 'wintune recommend' to see current ids.", id);
    return WT_ERR_NOT_FOUND;
}
