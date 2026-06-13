#include "output/text.h"
#include "output/table.h"
#include "common/units.h"
#include "system/power.h"
#include "platform/time.h"

#include <stdio.h>

void wt_print_recommendations_text(const WT_RecommendationList *recs)
{
    printf("Recommendations:\n");
    if (recs == NULL || recs->count == 0) {
        printf("  None. No performance issues detected from the current samples.\n");
        return;
    }

    for (size_t i = 0; i < recs->count; ++i) {
        const WT_Recommendation *r = &recs->items[i];
        printf("[%s] %s\n", r->id, r->title);
        printf("  Severity: %s | Risk: %s | Confidence: %d%%%s%s\n",
               wt_severity_to_string(r->severity),
               wt_risk_to_string(r->risk),
               r->confidence_percent,
               r->requires_admin ? " | requires admin" : "",
               r->rollback_available ? " | reversible" : "");
        printf("  Why: %s\n", r->reason);
        printf("  Action: %s\n", r->action);
        if (i + 1 < recs->count) {
            printf("\n");
        }
    }
}

void wt_print_scan_report_text(const WT_ScanReport *report,
                               const WT_RecommendationList *recs)
{
    if (report == NULL) {
        return;
    }

    printf("WinTune System Scan\n\n");

    /* System identity */
    if (report->os_ok) {
        wchar_t uptime[32];
        wt_format_duration_ms(report->os.uptime_ms, uptime, 32);
        printf("OS: %ls %ls\n", report->os.product_name, report->os.arch);
        printf("Host: %ls\n", report->os.hostname);
        printf("Uptime: %ls\n", uptime);
    } else {
        printf("OS: (unavailable)\n");
    }

    /* Power */
    if (report->power_ok) {
        printf("Power: %s", wt_power_scheme_name(report->power.scheme));
        if (report->power.on_ac == 1) {
            printf(" (AC");
        } else if (report->power.on_ac == 0) {
            printf(" (battery");
        } else {
            printf(" (");
        }
        if (report->power.battery_percent >= 0) {
            printf(" %d%%)", report->power.battery_percent);
        } else {
            printf(")");
        }
        printf("\n");
    }
    printf("\n");

    /* CPU */
    printf("CPU:\n");
    if (report->cpu_ok && report->cpu.available) {
        printf("  Usage: %.1f%%\n", report->cpu.total_usage_percent);
    } else {
        printf("  Usage: (unavailable)\n");
    }
    printf("  Logical processors: %u\n", report->cpu.logical_processor_count);
    printf("\n");

    /* Memory */
    printf("Memory:\n");
    if (report->memory_ok) {
        wchar_t used[32];
        wchar_t total[32];
        wt_format_bytes(report->memory.used_physical_bytes, used, 32);
        wt_format_bytes(report->memory.total_physical_bytes, total, 32);
        printf("  Used: %ls / %ls (%.1f%%)\n", used, total,
               report->memory.used_percent);
    } else {
        printf("  (unavailable)\n");
    }
    printf("\n");

    /* Disk */
    printf("Disk:\n");
    if (report->disk_ok && report->volume_count > 0) {
        for (size_t i = 0; i < report->volume_count; ++i) {
            const WT_DiskVolumeMetrics *v = &report->volumes[i];
            wchar_t free_bytes[32];
            wchar_t total_bytes[32];
            wt_format_bytes(v->free_bytes, free_bytes, 32);
            wt_format_bytes(v->total_bytes, total_bytes, 32);
            printf("  %ls %ls free / %ls (%.1f%% free)\n",
                   v->root_path, free_bytes, total_bytes, v->free_percent);
        }
    } else {
        printf("  (unavailable)\n");
    }
    if (report->disk_active_ok) {
        printf("  Active time: %.0f%%\n", report->disk_active_percent);
    }
    printf("\n");

    /* Boot (Phase 10) */
    if (report->boot_ok && report->boot.boot_duration_ms > 0) {
        wchar_t boot_dur[32];
        wt_format_duration_ms(report->boot.boot_duration_ms, boot_dur, 32);
        printf("Last boot: %ls", boot_dur);
        if (report->boot.is_degraded) {
            printf(" (degradation detected)");
        }
        printf("\n");
        if (report->boot.component_count > 0) {
            printf("  Slow components: %zu (see 'wintune boot analyze')\n",
                   report->boot.component_count);
        }
        printf("\n");
    }

    /* Top processes */
    printf("Top Processes by Memory:\n");
    if (report->processes_ok && report->top_process_count > 0) {
        wt_print_process_table(report->top_processes, report->top_process_count);
    } else {
        printf("  (unavailable)\n");
    }

    /* Recommendations */
    if (recs != NULL) {
        printf("\n");
        wt_print_recommendations_text(recs);
    }
}
