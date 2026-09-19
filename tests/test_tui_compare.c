#include "tui/tui_compare.h"

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

static void expect_near(double got, double want, double eps, const char *msg)
{
    double d = got - want;
    if (d < 0.0) {
        d = -d;
    }
    if (d > eps) {
        fprintf(stderr, "FAIL: %s (got %.4f want %.4f)\n", msg, got, want);
        g_failed = 1;
    }
}

int main(void)
{
    WT_TuiComparePair pair;
    WT_TuiCompareDelta d;

    memset(&pair, 0, sizeof(pair));
    wt_tui_compare_snap_clear(&pair.before);
    wt_tui_compare_snap_clear(&pair.after);

    wt_tui_compare_delta(&pair, &d);
    expect_true(!d.both_valid, "empty pair has no delta");

    wt_tui_compare_capture(&pair.before, 40.0, 60.0, 20.0, 1000.0, 500.0, 1,
                           8ULL * 1024 * 1024 * 1024,
                           16ULL * 1024 * 1024 * 1024, 1);
    expect_true(pair.before.valid, "before valid after capture");
    expect_true(pair.before.captured_utc[0] != '\0', "before has timestamp");

    wt_tui_compare_capture(&pair.after, 30.0, 55.0, 25.0, 2000.0, 400.0, 1,
                           7ULL * 1024 * 1024 * 1024,
                           16ULL * 1024 * 1024 * 1024, 1);
    expect_true(pair.after.valid, "after valid after capture");

    wt_tui_compare_delta(&pair, &d);
    expect_true(d.both_valid, "both snapshots yield delta");
    expect_near(d.cpu_pp, -10.0, 0.01, "cpu delta -10 pp");
    expect_near(d.mem_pp, -5.0, 0.01, "mem delta -5 pp");
    expect_near(d.disk_pp, 5.0, 0.01, "disk delta +5 pp");
    expect_near(d.net_rx_bps, 1000.0, 0.01, "net rx delta");
    expect_near(d.net_tx_bps, -100.0, 0.01, "net tx delta");
    expect_true(d.mem_used_bytes < 0, "mem used bytes decreased");

    /* Missing mem/net on one side should not invent deltas for those. */
    wt_tui_compare_capture(&pair.after, 30.0, -1.0, 25.0, 0.0, 0.0, 0, 0, 0, 0);
    wt_tui_compare_delta(&pair, &d);
    expect_true(d.both_valid, "pair still both_valid");
    expect_near(d.cpu_pp, -10.0, 0.01, "cpu still comparable");
    expect_near(d.disk_pp, 5.0, 0.01, "disk still comparable");
    expect_near(d.mem_pp, 0.0, 0.01, "mem delta skipped when after unknown");
    expect_near(d.net_rx_bps, 0.0, 0.01, "net delta skipped when after unknown");

    if (g_failed) {
        fputs("tui_compare tests failed\n", stderr);
        return 1;
    }
    fputs("tui_compare tests passed\n", stdout);
    return 0;
}
