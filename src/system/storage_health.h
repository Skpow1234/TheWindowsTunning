#ifndef WINTUNE_STORAGE_HEALTH_H
#define WINTUNE_STORAGE_HEALTH_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Read-only storage reliability signals (failure prediction when available).
 * Never wipes, formats, or "repairs" disks. */

typedef enum WT_StorageHealthStatus {
    WT_STORAGE_HEALTH_UNKNOWN = 0,
    WT_STORAGE_HEALTH_OK,
    WT_STORAGE_HEALTH_WARNING,
    WT_STORAGE_HEALTH_DEGRADED,
    WT_STORAGE_HEALTH_UNAVAILABLE
} WT_StorageHealthStatus;

typedef struct WT_StorageDiskHealth {
    unsigned int physical_drive; /* PhysicalDriveN index */
    wchar_t model[128];
    wchar_t serial[64];
    wchar_t bus_type[32];
    wchar_t media_hint[32]; /* "ssd", "hdd", "unknown" */
    WT_StorageHealthStatus status;
    int predict_failure; /* 1 = predicted, 0 = not, -1 = not queried */
    int descriptor_ok;
    int predict_ok;
    wchar_t note[192];
} WT_StorageDiskHealth;

#define WT_MAX_STORAGE_DISKS 16

typedef struct WT_StorageHealthReport {
    size_t disk_count;
    WT_StorageDiskHealth disks[WT_MAX_STORAGE_DISKS];
    int any_degraded;
    int any_warning;
    int any_unavailable;
    wchar_t note[256];
} WT_StorageHealthReport;

void wt_storage_health_init(WT_StorageHealthReport *report);

/* Enumerates \\.\PhysicalDriveN and queries Storage Failure Prediction +
 * device identity. Partial success is OK (some disks may be unavailable). */
WT_Result wt_collect_storage_health(WT_StorageHealthReport *report);

const char *wt_storage_health_status_name(WT_StorageHealthStatus status);

#endif /* WINTUNE_STORAGE_HEALTH_H */
