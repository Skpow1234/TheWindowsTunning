#include "output/text.h"
#include "output/table.h"
#include "common/units.h"

#include <stdio.h>

void wt_print_scan_report_text(const WT_ScanReport *report)
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
    printf("\n");

    /* Top processes */
    printf("Top Processes by Memory:\n");
    if (report->processes_ok && report->top_process_count > 0) {
        wt_print_process_table(report->top_processes, report->top_process_count);
    } else {
        printf("  (unavailable)\n");
    }
}
