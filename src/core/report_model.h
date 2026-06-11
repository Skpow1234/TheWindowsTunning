#ifndef WINTUNE_REPORT_MODEL_H
#define WINTUNE_REPORT_MODEL_H

#include <stddef.h>

#include "common/error.h"
#include "metrics/cpu.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "system/os_info.h"
#include "system/power.h"

#define WT_MAX_VOLUMES 32
#define WT_MAX_TOP_PROCESSES 64

/* The stable in-memory model produced by a scan and consumed by every output
 * format (text now; JSON/TUI later). Per-section `*_ok` flags allow partial
 * results: a section that failed to collect is simply reported as unavailable
 * rather than aborting the whole scan. */
typedef struct WT_ScanReport {
    WT_OsInfo os;
    WT_CpuMetrics cpu;
    WT_MemoryMetrics memory;

    WT_DiskVolumeMetrics volumes[WT_MAX_VOLUMES];
    size_t volume_count;
    double disk_active_percent;

    WT_ProcessInfo top_processes[WT_MAX_TOP_PROCESSES];
    size_t top_process_count;

    WT_PowerInfo power;

    int os_ok;
    int cpu_ok;
    int memory_ok;
    int disk_ok;
    int disk_active_ok;
    int processes_ok;
    int power_ok;
} WT_ScanReport;

/* Zero-initializes a report and clears all availability flags. */
WT_Result wt_scan_report_init(WT_ScanReport *report);

#endif /* WINTUNE_REPORT_MODEL_H */
