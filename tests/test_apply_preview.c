#include "actions/apply_preview.h"
#include "actions/action_map.h"
#include "system/power.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_int(int got, int want, const char *label)
{
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", label, got, want);
        g_failed = 1;
    }
}

static void expect_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        g_failed = 1;
    }
}

int main(void)
{
    WT_ApplyPreview preview;
    WT_ApplyRequest req;

    /* Power preview is read-only and should never mutate. */
    expect_int(wt_apply_preview_power(WT_POWER_HIGH_PERF, &preview), WT_OK,
               "preview power high");
    expect_true(preview.action_type[0] != L'\0', "power action_type set");
    expect_true(preview.previous_value[0] != L'\0' || preview.would_mutate == 0
                    || preview.summary[0] != '\0',
                "power previous or summary");
    expect_true(preview.rollback_available == 1, "power rollback available");
    expect_true(wcscmp(preview.rec_id, L"WT-POWER-001") == 0
                    || wcscmp(preview.rec_id, L"WT-POWER-002") == 0,
                "power rec id");

    /* Already-on-target still OK with would_mutate=0 when re-previewing current. */
    WT_PowerInfo cur;
    if (wt_collect_power_info(&cur) == WT_OK && cur.scheme != WT_POWER_UNKNOWN) {
        expect_int(wt_apply_preview_power(cur.scheme, &preview), WT_OK,
                   "preview current plan");
        expect_int(preview.would_mutate, 0, "current plan no mutate");
    }

    memset(&req, 0, sizeof(req));
    req.rec_id = L"WT-POWER-001";
    expect_int(wt_apply_preview_from_request(&req, &preview), WT_OK,
               "preview from request power");
    expect_true(preview.summary[0] != '\0', "request summary");

    req.rec_id = L"WT-MEMORY-001";
    expect_int(wt_apply_preview_from_request(&req, &preview),
               WT_ERR_NOT_SUPPORTED, "advisory preview");
    expect_int(preview.would_mutate, 0, "advisory no mutate");
    expect_int(preview.blocked, 1, "advisory blocked");

    req.rec_id = L"WT-STARTUP-DISABLE";
    req.target_id = NULL;
    expect_int(wt_apply_preview_from_request(&req, &preview),
               WT_ERR_INVALID_ARGUMENT, "startup disable needs target");

    req.rec_id = L"NOT-A-REAL-ID";
    expect_int(wt_apply_preview_from_request(&req, &preview), WT_ERR_NOT_FOUND,
               "unknown id");

    if (g_failed) {
        fputs("apply preview tests failed\n", stderr);
        return 1;
    }
    fputs("apply preview tests passed\n", stdout);
    return 0;
}
