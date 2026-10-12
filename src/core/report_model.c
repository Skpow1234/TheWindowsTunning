#include "core/report_model.h"

#include <string.h>

const char *wt_collector_name(int id)
{
    switch (id) {
    case WT_COLLECTOR_OS:
        return "os";
    case WT_COLLECTOR_MEMORY:
        return "memory";
    case WT_COLLECTOR_CPU:
        return "cpu";
    case WT_COLLECTOR_NETWORK:
        return "network";
    case WT_COLLECTOR_DISK:
        return "disk";
    case WT_COLLECTOR_DISK_IO:
        return "disk_io";
    case WT_COLLECTOR_POWER:
        return "power";
    case WT_COLLECTOR_GPU:
        return "gpu";
    case WT_COLLECTOR_AUDIO:
        return "audio";
    case WT_COLLECTOR_SEARCH:
        return "search";
    case WT_COLLECTOR_DEFENDER:
        return "defender";
    case WT_COLLECTOR_BOOT:
        return "boot";
    case WT_COLLECTOR_UPDATES:
        return "updates";
    case WT_COLLECTOR_PROCESSES:
        return "processes";
    default:
        return "unknown";
    }
}

WT_Result wt_scan_report_init(WT_ScanReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    report->disk_read_bytes_per_sec = -1.0;
    report->disk_write_bytes_per_sec = -1.0;
    report->disk_avg_queue_length = -1.0;
    report->disk_read_latency_ms_p50 = -1.0;
    report->disk_read_latency_ms_p95 = -1.0;
    report->disk_write_latency_ms_p50 = -1.0;
    report->disk_write_latency_ms_p95 = -1.0;
    report->disk_active_max_percent = -1.0;
    report->cpu_max_percent = -1.0;
    report->cpu.total_usage_percent = -1.0;
    report->cpu.dpc_percent = -1.0;
    report->cpu.interrupt_percent = -1.0;
    report->memory_max_used_percent = -1.0;
    report->gpu.max_utilization_percent = -1.0;
    return WT_OK;
}
