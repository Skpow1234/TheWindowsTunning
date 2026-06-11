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

#endif /* WINTUNE_DISK_H */
