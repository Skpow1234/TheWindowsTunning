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

/* Enumerates fixed (DRIVE_FIXED) volumes and their free/total space.
 * Writes up to `capacity` entries and reports the count via `out_count`. */
WT_Result wt_collect_disk_volumes(WT_DiskVolumeMetrics *out,
                                  size_t capacity,
                                  size_t *out_count);

/* Samples total physical-disk active time as a percentage via PDH
 * (\PhysicalDisk(_Total)\% Disk Time). May exceed 100 on multi-disk systems;
 * the caller decides whether to clamp for display. */
WT_Result wt_collect_disk_activity(unsigned int sample_ms, double *out_percent);

#endif /* WINTUNE_DISK_H */
