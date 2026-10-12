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
    /* LogicalDisk activity for this volume (Phase 26). -1 / flags when n/a. */
    double active_percent;
    double read_bytes_per_sec;
    double write_bytes_per_sec;
    double avg_queue_length;
    int activity_ok;
    int throughput_ok;
    int queue_ok;
    /* Phase 59: Avg. Disk sec/Read and sec/Write, in milliseconds.
     * Percentiles are over scan sample windows (each window is already an
     * average), not over individual I/Os. -1 when that side was not read. */
    double read_latency_ms_p50;
    double read_latency_ms_p95;
    double write_latency_ms_p50;
    double write_latency_ms_p95;
    double read_latency_samples[32];
    double write_latency_samples[32];
    unsigned int read_latency_count;
    unsigned int write_latency_count;
} WT_DiskVolumeMetrics;

/* System-wide PhysicalDisk(_Total) sample (Phase 25). Rates are bytes/sec.
 * avg_queue_length is -1.0 when that counter is unavailable. */
typedef struct WT_DiskIoMetrics {
    double active_percent;
    double read_bytes_per_sec;
    double write_bytes_per_sec;
    double avg_queue_length;
    double read_latency_ms;  /* -1 when Avg. Disk sec/Read is unavailable */
    double write_latency_ms; /* -1 when Avg. Disk sec/Write is unavailable */
    int active_ok;
    int throughput_ok;
    int queue_ok;
    int latency_ok;
} WT_DiskIoMetrics;

#define WT_DISK_LATENCY_CAP 32

/* Appends one sample-window latency (milliseconds) and refreshes nearest-rank
 * p50/p95. Ignores negative samples. Stops storing once `cap` is reached. */
void wt_disk_latency_push(double *samples,
                          unsigned int *count,
                          unsigned int cap,
                          double latency_ms,
                          double *p50,
                          double *p95);

/* Enumerates fixed (DRIVE_FIXED) volumes and their free/total space.
 * Writes up to `capacity` entries and reports the count via `out_count`.
 * Activity fields are left unavailable until wt_enrich_disk_volume_io(). */
WT_Result wt_collect_disk_volumes(WT_DiskVolumeMetrics *out,
                                  size_t capacity,
                                  size_t *out_count);

/* Samples LogicalDisk activity for each volume in one PDH window
 * (% Disk Time, Read/Write Bytes/sec, Avg. Disk Queue Length). */
WT_Result wt_enrich_disk_volume_io(WT_DiskVolumeMetrics *volumes,
                                   size_t count,
                                   unsigned int sample_ms);

/* Samples total physical-disk active time as a percentage via PDH
 * (\PhysicalDisk(_Total)\% Disk Time). May exceed 100 on multi-disk systems;
 * the caller decides whether to clamp for display. */
WT_Result wt_collect_disk_activity(unsigned int sample_ms, double *out_percent);

/* Samples PhysicalDisk(_Total) and, when volumes != NULL, LogicalDisk per
 * volume in a single PDH window (one sample interval). */
WT_Result wt_collect_disk_io_ex(unsigned int sample_ms,
                                WT_DiskIoMetrics *total_out,
                                WT_DiskVolumeMetrics *volumes,
                                size_t volume_count);

/* Convenience: total counters only. */
WT_Result wt_collect_disk_io(unsigned int sample_ms, WT_DiskIoMetrics *out);

#endif /* WINTUNE_DISK_H */
