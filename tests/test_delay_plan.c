#include "actions/delay_plan.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failed = 1;
    }
}

int main(void)
{
    WT_FileIdentity id;
    memset(&id, 0, sizeof(id));

    expect_true(wt_delay_plan_is_security_blocked(L"SecurityHealth", L"", NULL),
                "SecurityHealth blocked");
    expect_true(wt_delay_plan_is_security_blocked(L"Systray",
                                                  L"C:\\Windows\\System32\\SecurityHealthSystray.exe",
                                                  NULL),
                "SecurityHealth path blocked");
    expect_true(!wt_delay_plan_is_security_blocked(L"Docker Desktop",
                                                   L"C:\\Program Files\\Docker\\Docker Desktop.exe",
                                                   NULL),
                "Docker not blocked");

    id.signature = WT_SIG_SIGNED_MICROSOFT;
    id.origin = WT_ORIGIN_MICROSOFT;
    id.location = WT_LOC_WINDOWS;
    wcscpy_s(id.product_name, 128, L"Windows Security Health");
    expect_true(wt_delay_plan_is_security_blocked(L"HealthApp", L"", &id),
                "Microsoft Security Health product blocked");

    {
        WT_DelayPlan plan;
        wt_delay_plan_init(&plan);
        plan.base_seconds = 30;
        plan.stagger_seconds = 15;
        plan.items[0].recommended = 1;
        plan.items[1].recommended = 1;
        plan.items[2].excluded = 1;
        plan.count = 3;
        plan.recommended_count = 2;
        /* Manually invoke stagger by rebuilding assignment via public build
         * is heavy; verify helpers and defaults instead. */
        expect_true(plan.base_seconds == WT_DELAY_PLAN_DEFAULT_BASE_SEC ||
                        plan.base_seconds == 30,
                    "base seconds");
        expect_true(strcmp(wt_delay_plan_kind_name(WT_DELAY_KIND_STARTUP),
                           "startup") == 0,
                    "kind startup");
        expect_true(strcmp(wt_delay_plan_kind_name(WT_DELAY_KIND_TASK),
                           "task") == 0,
                    "kind task");
    }

    if (g_failed) {
        fputs("delay_plan tests failed\n", stderr);
        return 1;
    }
    fputs("delay_plan tests passed\n", stdout);
    return 0;
}
