#include "core/scan.h"

#include "metrics/cpu.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "system/os_info.h"
#include "system/power.h"
#include "system/boot.h"
#include "system/updates.h"
#include "common/error.h"
#include "common/log.h"

#include <windows.h>

#define WT_SCAN_DEFAULT_SAMPLE_MS 500
#define WT_SCAN_DEFAULT_TOP_LIMIT 10
#define WT_SCAN_DEFAULT_SAMPLE_COUNT 1
#define WT_SCAN_DEFAULT_SAMPLE_INTERVAL_MS 1000
#define WT_SCAN_MAX_SAMPLES 32

static void wt_scan_collect_top_processes(WT_ScanReport *report, size_t top_limit,
                                          unsigned int sample_ms)
{
    if (top_limit > WT_MAX_TOP_PROCESSES) {
        top_limit = WT_MAX_TOP_PROCESSES;
    }

    size_t count = 0;
    if (wt_collect_top_processes(report->top_processes, top_limit,
                                 WT_PROCESS_SORT_MEMORY, sample_ms,
                                 &count) == WT_OK) {
        report->top_process_count = count;
        report->processes_ok = 1;
    }
}

static void wt_scan_merge_sample(WT_ScanReport *acc, const WT_ScanReport *sample,
                                 unsigned int sample_index,
                                 unsigned int sample_total)
{
    unsigned int scan_samples = acc->scan_sample_count;
    unsigned int scan_interval = acc->scan_sample_interval_ms;

    if (sample_index == 0) {
        *acc = *sample;
        acc->scan_sample_count = scan_samples;
        acc->scan_sample_interval_ms = scan_interval;
        return;
    }

    if (sample->cpu_ok && sample->cpu.available && acc->cpu_ok) {
        acc->cpu.total_usage_percent =
            ((acc->cpu.total_usage_percent * (double)sample_index) +
             sample->cpu.total_usage_percent) /
            (double)(sample_index + 1);
    }

    if (sample->disk_active_ok && acc->disk_active_ok) {
        acc->disk_active_percent =
            ((acc->disk_active_percent * (double)sample_index) +
             sample->disk_active_percent) /
            (double)(sample_index + 1);
    }

    (void)sample_total;
}

static WT_Result wt_run_scan_once(const WT_ScanOptions *opts,
                                  WT_ScanReport *report)
{
    unsigned int sample_ms = WT_SCAN_DEFAULT_SAMPLE_MS;
    size_t top_limit = WT_SCAN_DEFAULT_TOP_LIMIT;
    if (opts != NULL) {
        if (opts->cpu_sample_ms > 0) {
            sample_ms = opts->cpu_sample_ms;
        }
        if (opts->top_limit > 0) {
            top_limit = opts->top_limit;
        }
    }

    report->os_ok = (wt_collect_os_info(&report->os) == WT_OK);
    report->memory_ok = (wt_collect_memory_metrics(&report->memory) == WT_OK);
    report->cpu_ok = (wt_collect_cpu_metrics(sample_ms, &report->cpu) == WT_OK);
    report->disk_ok =
        (wt_collect_disk_volumes(report->volumes, WT_MAX_VOLUMES,
                                 &report->volume_count) == WT_OK);
    report->disk_active_ok =
        (wt_collect_disk_activity(sample_ms, &report->disk_active_percent) ==
         WT_OK);
    report->power_ok = (wt_collect_power_info(&report->power) == WT_OK);

    WT_Result boot_r = wt_collect_boot_from_event_log(&report->boot);
    report->boot_ok = (boot_r == WT_OK);
    if (boot_r != WT_OK && boot_r != WT_ERR_NOT_FOUND) {
        WT_LOGD("boot metrics unavailable (%s)", wt_result_to_string(boot_r));
    }

    WT_Result upd_r = wt_collect_update_status_fast(&report->updates);
    report->updates_ok = (upd_r == WT_OK);

    wt_scan_collect_top_processes(report, top_limit, sample_ms);

    return WT_OK;
}

WT_Result wt_run_scan(const WT_ScanOptions *opts, WT_ScanReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_scan_report_init(report);

    unsigned int sample_count = WT_SCAN_DEFAULT_SAMPLE_COUNT;
    unsigned int sample_interval_ms = WT_SCAN_DEFAULT_SAMPLE_INTERVAL_MS;
    if (opts != NULL) {
        if (opts->sample_count > 0) {
            sample_count = opts->sample_count;
        }
        if (opts->sample_interval_ms > 0) {
            sample_interval_ms = opts->sample_interval_ms;
        }
    }
    if (sample_count > WT_SCAN_MAX_SAMPLES) {
        sample_count = WT_SCAN_MAX_SAMPLES;
    }
    if (sample_count == 0) {
        sample_count = WT_SCAN_DEFAULT_SAMPLE_COUNT;
    }

    report->scan_sample_count = sample_count;
    report->scan_sample_interval_ms = sample_interval_ms;

    WT_ScanReport sample;
    for (unsigned int i = 0; i < sample_count; ++i) {
        wt_scan_report_init(&sample);
        WT_Result r = wt_run_scan_once(opts, &sample);
        if (r != WT_OK) {
            return r;
        }
        wt_scan_merge_sample(report, &sample, i, sample_count);
        if (i + 1 < sample_count) {
            Sleep(sample_interval_ms);
        }
    }

    return WT_OK;
}
