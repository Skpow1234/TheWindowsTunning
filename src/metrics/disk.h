#ifndef WINTUNE_DISK_H
#define WINTUNE_DISK_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

typedef struct WT_DiskVolumeMetrics {
    wchar_t root_path[16];               /* e.g. "C:\\" */
    unsigned long long total_bytes;
    unsigned long long free_bytes;
    double free_percent;
} WT_DiskVolumeMetrics;

/* System-wide PhysicalDisk(_Total) sample (Phase 25). Rates are bytes/sec.
 * avg_queue_length is -1.0 when that counter is unavailable. */
typedef struct WT_DiskIoMetrics {
    double active_percent;
    double read_bytes_per_sec;
    double write_bytes_per_sec;
    double avg_queue_length;
    int active_ok;
    int throughput_ok;
    int queue_ok;
} WT_DiskIoMetrics;

/* Enumerates fixed (DRIVE_FIXED) volumes and their free/total space.
 * Writes up to `capacity` entries and reports the count via `out_count`. */
WT_Result wt_collect_disk_volumes(WT_DiskVolumeMetrics *out,
                                  size_t capacity,
                                  size_t *out_count);

/* Samples total physical-disk active time as a percentage via PDH
 * (\PhysicalDisk(_Total)\% Disk Time). May exceed 100 on multi-disk systems;
 * the caller decides whether to clamp for display. */
WT_Result wt_collect_disk_activity(unsigned int sample_ms, double *out_percent);

/* One PDH sample window for active %, read/write bytes/sec, and queue length.
 * Prefer this in scan paths to avoid paying the sample interval twice. */
WT_Result wt_collect_disk_io(unsigned int sample_ms, WT_DiskIoMetrics *out);

#endif /* WINTUNE_DISK_H */
