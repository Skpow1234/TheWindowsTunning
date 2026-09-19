#include "core/confidence.h"
#include "core/report_model.h"

#include <stdio.h>

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
    WT_ScanReport rep;
    char basis[96];
    int conf;

    wt_scan_report_init(&rep);
    rep.scan_sample_count = 1;
    rep.scan_sample_interval_ms = 1000;
    rep.cpu_ok = 1;
    rep.cpu.available = 1;
    rep.cpu.total_usage_percent = 90.0;
    rep.cpu_ok_samples = 1;
    rep.cpu_hot_samples = 1;
    rep.cpu_max_percent = 90.0;

    conf = wt_confidence_compute(&rep, WT_CONF_CPU, 75);
    expect_true(conf < 75, "single-sample CPU confidence penalized");
    expect_true(!wt_confidence_should_emit(&rep, WT_CONF_CPU, conf),
                "suppress single-sample CPU at 90%");

    rep.cpu.total_usage_percent = 96.0;
    rep.cpu_max_percent = 96.0;
    conf = wt_confidence_compute(&rep, WT_CONF_CPU, 75);
    expect_true(wt_confidence_should_emit(&rep, WT_CONF_CPU, conf),
                "allow extreme single-sample CPU >= 95%");

    rep.scan_sample_count = 5;
    rep.scan_sample_interval_ms = 1000;
    rep.cpu_ok_samples = 5;
    rep.cpu_hot_samples = 4;
    rep.cpu.total_usage_percent = 88.0;
    rep.cpu_max_percent = 92.0;
    conf = wt_confidence_compute(&rep, WT_CONF_CPU, 75);
    expect_true(conf > 75, "multi-sample CPU confidence boosted");
    expect_true(wt_confidence_should_emit(&rep, WT_CONF_CPU, conf),
                "emit multi-sample CPU");

    wt_confidence_basis(&rep, WT_CONF_CPU, basis, sizeof(basis));
    expect_true(basis[0] != '\0', "basis non-empty");

    conf = wt_confidence_compute(&rep, WT_CONF_SNAPSHOT, 85);
    expect_true(conf == 85, "snapshot confidence unchanged");

    if (g_failed) {
        fputs("confidence tests failed\n", stderr);
        return 1;
    }
    fputs("confidence tests passed\n", stdout);
    return 0;
}
