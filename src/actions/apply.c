#include "actions/apply.h"
#include "actions/safe_actions.h"
#include "core/scan.h"
#include "core/recommendations.h"

#include <windows.h>
#include <strsafe.h>
#include <wchar.h>

/* Returns 1 and fills *out if `id` maps to an actionable power scheme. */
static int wt_apply_power_target(const wchar_t *id, WT_PowerScheme *out)
{
    if (_wcsicmp(id, L"WT-POWER-001") == 0) {
        *out = WT_POWER_HIGH_PERF;
        return 1;
    }
    if (_wcsicmp(id, L"WT-POWER-002") == 0) {
        *out = WT_POWER_BALANCED;
        return 1;
    }
    return 0;
}

/* Checks whether `id` is a currently-generated recommendation (so we can tell
 * "advisory, no action" apart from "unknown id"). */
static int wt_id_is_current_recommendation(const wchar_t *id)
{
    WT_ScanOptions scan_opts;
    scan_opts.cpu_sample_ms = 0;
    scan_opts.top_limit = 10;

    WT_ScanReport report;
    if (wt_run_scan(&scan_opts, &report) != WT_OK) {
        return 0;
    }

    WT_RecommendationList recs;
    if (wt_generate_recommendations(&report, &recs) != WT_OK) {
        return 0;
    }

    char want[64];
    if (FAILED(StringCchPrintfA(want, sizeof(want), "%ls", id))) {
        return 0;
    }
    for (size_t i = 0; i < recs.count; ++i) {
        if (_stricmp(recs.items[i].id, want) == 0) {
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
    if (wt_apply_power_target(id, &target)) {
        return wt_action_set_power_plan(target, assume_yes, msg, msg_cap);
    }

    if (wt_id_is_current_recommendation(id)) {
        StringCchPrintfA(msg, msg_cap,
                         "'%ls' is advisory: it has no automatic action. "
                         "Follow the suggested steps from 'wintune recommend'.",
                         id);
        return WT_ERR_NOT_SUPPORTED;
    }

    StringCchPrintfA(msg, msg_cap,
                     "'%ls' is not a current recommendation. "
                     "Run 'wintune recommend' to see available ids.", id);
    return WT_ERR_NOT_FOUND;
}
