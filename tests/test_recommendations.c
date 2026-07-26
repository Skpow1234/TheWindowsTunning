#include "core/recommendations.h"
#include "core/report_model.h"
#include "system/power.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void expect_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        g_failed = 1;
    }
}

static int list_has_id(const WT_RecommendationList *list, const char *id)
{
    if (list == NULL || id == NULL) {
        return 0;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    expect_true(strcmp(wt_severity_to_string(WT_SEVERITY_HIGH), "high") == 0,
                "severity high");
    expect_true(strcmp(wt_risk_to_string(WT_RISK_LOW), "low") == 0, "risk low");
    expect_true(wt_generate_recommendations(NULL, NULL) == WT_ERR_INVALID_ARGUMENT,
                "null args");

    WT_ScanReport report;
    WT_RecommendationList list;

    /* Healthy memory / CPU / disk — should not emit pressure IDs. */
    wt_scan_report_init(&report);
    report.memory_ok = 1;
    report.memory.total_physical_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
    report.memory.available_physical_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    report.memory.used_percent = 50.0;
    report.cpu_ok = 1;
    report.cpu.available = 1;
    report.cpu.total_usage_percent = 10.0;
    report.disk_ok = 1;
    report.disk_active_ok = 1;
    report.disk_active_percent = 20.0;
    report.volume_count = 1;
    report.volumes[0].free_percent = 40.0;
    wcscpy(report.volumes[0].root_path, L"C:\\");
    report.power_ok = 0; /* skip live-ish power from machine */

    expect_true(wt_generate_recommendations(&report, &list) == WT_OK, "gen ok");
    expect_true(!list_has_id(&list, "WT-MEMORY-001"), "no memory pressure");
    expect_true(!list_has_id(&list, "WT-CPU-001"), "no cpu pressure");
    expect_true(!list_has_id(&list, "WT-DISK-001"), "no disk active pressure");

    /* Low available memory => WT-MEMORY-001 */
    wt_scan_report_init(&report);
    report.memory_ok = 1;
    report.memory.total_physical_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    report.memory.available_physical_bytes = 1ULL * 1024ULL * 1024ULL * 1024ULL; /* ~6% */
    report.memory.used_percent = 94.0;
    expect_true(wt_generate_recommendations(&report, &list) == WT_OK, "gen mem");
    expect_true(list_has_id(&list, "WT-MEMORY-001"), "memory pressure id");

    /* High disk active => WT-DISK-001 */
    wt_scan_report_init(&report);
    report.disk_active_ok = 1;
    report.disk_active_percent = 95.0;
    expect_true(wt_generate_recommendations(&report, &list) == WT_OK, "gen disk");
    expect_true(list_has_id(&list, "WT-DISK-001"), "disk active id");

    /* AC + Balanced => WT-POWER-001 */
    wt_scan_report_init(&report);
    report.power_ok = 1;
    report.power.on_ac = 1;
    report.power.scheme = WT_POWER_BALANCED;
    wcscpy(report.power.active_name, L"Balanced");
    expect_true(wt_generate_recommendations(&report, &list) == WT_OK, "gen power");
    expect_true(list_has_id(&list, "WT-POWER-001"), "power on AC id");

    if (g_failed) {
        fputs("recommendations tests failed\n", stderr);
        return 1;
    }
    fputs("recommendations tests passed\n", stdout);
    return 0;
}
