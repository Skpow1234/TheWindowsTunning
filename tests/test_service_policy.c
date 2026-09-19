#include "service/service_policy.h"

#include <stdio.h>
#include <string.h>

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
    WT_ServicePolicy p;

    wt_service_policy_defaults(&p);
    expect_true(strcmp(p.name, "balanced") == 0, "default name");
    expect_true(p.scan_interval_ms == 15u * 60u * 1000u, "default interval");
    expect_true(p.sample_count == 3, "default samples");
    expect_true(p.history_keep == 2, "default history");

    expect_true(wt_service_policy_from_name(L"performance", &p) == WT_OK,
                "perf ok");
    expect_true(strcmp(p.name, "performance") == 0, "perf name");
    expect_true(p.scan_interval_ms == 5u * 60u * 1000u, "perf interval");
    expect_true(p.sample_count == 5, "perf samples");

    expect_true(wt_service_policy_from_name(L"light", &p) == WT_OK, "light ok");
    expect_true(p.scan_interval_ms == 60u * 60u * 1000u, "light interval");
    expect_true(p.sample_count == 1, "light samples");

    expect_true(wt_service_policy_from_name(L"battery", &p) == WT_OK,
                "battery alias");
    expect_true(strcmp(p.name, "light") == 0, "battery -> light");

    expect_true(wt_service_policy_from_name(L"on-demand", &p) == WT_OK,
                "on-demand ok");
    expect_true(p.scan_interval_ms == 0, "on-demand no timer");
    expect_true(wt_service_policy_from_name(L"manual", &p) == WT_OK,
                "manual alias");

    expect_true(wt_service_policy_from_name(L"nope", &p) == WT_ERR_NOT_FOUND,
                "unknown fails");

    expect_true(wt_service_policy_describe(&p) != NULL, "describe non-null");

    if (g_failed) {
        fputs("service_policy tests failed\n", stderr);
        return 1;
    }
    fputs("service_policy tests passed\n", stdout);
    return 0;
}
