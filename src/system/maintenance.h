#ifndef WINTUNE_MAINTENANCE_H
#define WINTUNE_MAINTENANCE_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "core/report_model.h"

/* Correlate high CPU/disk samples with Defender / WU / optimization activity.
 * Read-only — never disables security or Windows Update. */

typedef enum WT_MaintKind {
    WT_MAINT_UNKNOWN = 0,
    WT_MAINT_DEFENDER,
    WT_MAINT_WINDOWS_UPDATE,
    WT_MAINT_OPTIMIZATION
} WT_MaintKind;

typedef struct WT_MaintHit {
    WT_MaintKind kind;
    wchar_t source[16]; /* "process" or "task" */
    wchar_t name[160];
    double cpu_percent;       /* -1 if n/a */
    double disk_bytes_per_sec; /* -1 if n/a */
    int task_running;
    int last_run_recent; /* LastRun within lookback window */
    char last_run_utc[32];
} WT_MaintHit;

#define WT_MAX_MAINT_HITS 24

typedef struct WT_MaintenanceReport {
    int scan_ok;
    unsigned sample_count;
    double cpu_percent;
    double disk_active_percent;
    unsigned cpu_hot_samples;
    unsigned disk_hot_samples;
    int cpu_hot;  /* sustained or peak hot during scan */
    int disk_hot;

    size_t hit_count;
    WT_MaintHit hits[WT_MAX_MAINT_HITS];

    int defender_active;
    int update_active;
    int optimize_active;
    int overlap; /* hot samples AND at least one maint hit */

    wchar_t note[320];
} WT_MaintenanceReport;

void wt_maintenance_init(WT_MaintenanceReport *report);

/* Correlate an existing scan report (plus task probes). */
WT_Result wt_maintenance_from_scan(WT_MaintenanceReport *report,
                                   const WT_ScanReport *scan);

const char *wt_maint_kind_name(WT_MaintKind kind);

#endif /* WINTUNE_MAINTENANCE_H */
