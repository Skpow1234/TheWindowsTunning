#include "core/scan.h"

#include "metrics/cpu.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "system/os_info.h"
#include "system/power.h"
#include "system/boot.h"
#include "common/error.h"
#include "common/log.h"

#define WT_SCAN_DEFAULT_SAMPLE_MS 500
#define WT_SCAN_DEFAULT_TOP_LIMIT 10

static void wt_collect_top_processes(WT_ScanReport *report, size_t top_limit)
{
    if (top_limit > WT_MAX_TOP_PROCESSES) {
        top_limit = WT_MAX_TOP_PROCESSES;
    }

    size_t count = 0;
    if (wt_collect_top_processes_by_memory(report->top_processes, top_limit,
                                           &count) == WT_OK) {
        report->top_process_count = count;
        report->processes_ok = 1;
    }
}

WT_Result wt_run_scan(const WT_ScanOptions *opts, WT_ScanReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_scan_report_init(report);

    unsigned int sample_ms = WT_SCAN_DEFAULT_SAMPLE_MS;
    size_t top_limit = WT_SCAN_DEFAULT_TOP_LIMIT;
    if (opts != NULL) {
        if (opts->cpu_sample_ms > 0) sample_ms = opts->cpu_sample_ms;
        if (opts->top_limit > 0) top_limit = opts->top_limit;
    }

    report->os_ok = (wt_collect_os_info(&report->os) == WT_OK);
    report->memory_ok = (wt_collect_memory_metrics(&report->memory) == WT_OK);
    report->cpu_ok = (wt_collect_cpu_metrics(sample_ms, &report->cpu) == WT_OK);
    report->disk_ok =
        (wt_collect_disk_volumes(report->volumes, WT_MAX_VOLUMES,
                                 &report->volume_count) == WT_OK);
    report->disk_active_ok =
        (wt_collect_disk_activity(sample_ms, &report->disk_active_percent) == WT_OK);
    report->power_ok = (wt_collect_power_info(&report->power) == WT_OK);

    WT_Result boot_r = wt_collect_boot_from_event_log(&report->boot);
    report->boot_ok = (boot_r == WT_OK);
    if (boot_r != WT_OK && boot_r != WT_ERR_NOT_FOUND) {
        WT_LOGD("boot metrics unavailable (%s)", wt_result_to_string(boot_r));
    }

    wt_collect_top_processes(report, top_limit);

    return WT_OK;
}
