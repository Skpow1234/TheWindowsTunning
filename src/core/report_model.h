#ifndef WINTUNE_REPORT_MODEL_H
#define WINTUNE_REPORT_MODEL_H

#include <stddef.h>

#include "common/error.h"
#include "metrics/cpu.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "metrics/gpu.h"
#include "metrics/network.h"
#include "system/os_info.h"
#include "system/power.h"
#include "system/boot.h"
#include "system/updates.h"
#include "system/audio.h"
#include "system/search.h"
#include "system/defender.h"

#define WT_MAX_VOLUMES 32
#define WT_MAX_TOP_PROCESSES 32

/* Phase 71: wall time of each scan collector, in milliseconds. Network is
 * the two adapter snapshots only. CPU, disk I/O, GPU, and process times
 * include their sample waits. */
typedef enum WT_CollectorId {
    WT_COLLECTOR_OS = 0,
    WT_COLLECTOR_MEMORY,
    WT_COLLECTOR_CPU,
    WT_COLLECTOR_NETWORK,
    WT_COLLECTOR_DISK,
    WT_COLLECTOR_DISK_IO,
    WT_COLLECTOR_POWER,
    WT_COLLECTOR_GPU,
    WT_COLLECTOR_AUDIO,
    WT_COLLECTOR_SEARCH,
    WT_COLLECTOR_DEFENDER,
    WT_COLLECTOR_BOOT,
    WT_COLLECTOR_UPDATES,
    WT_COLLECTOR_PROCESSES,
    WT_COLLECTOR_COUNT
} WT_CollectorId;

const char *wt_collector_name(int id);

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
    double disk_active_max_percent;   /* peak across samples; -1 if n/a */
    unsigned int disk_active_ok_samples;
    unsigned int disk_active_hot_samples; /* samples with active >= 90% */
    double disk_read_bytes_per_sec;   /* PhysicalDisk(_Total); -1 if n/a */
    double disk_write_bytes_per_sec;
    double disk_avg_queue_length;     /* -1 if n/a */
    double disk_read_latency_ms[32];  /* Phase 59 sample windows, milliseconds */
    double disk_write_latency_ms[32];
    unsigned int disk_read_latency_count;
    unsigned int disk_write_latency_count;
    double disk_read_latency_ms_p50;  /* -1 if n/a */
    double disk_read_latency_ms_p95;
    double disk_write_latency_ms_p50;
    double disk_write_latency_ms_p95;

    /* Phase 35: multi-sample CPU / memory confidence inputs */
    double cpu_max_percent;           /* -1 if n/a */
    unsigned int cpu_ok_samples;
    unsigned int cpu_hot_samples;     /* samples with CPU >= 85% */
    double memory_max_used_percent;   /* -1 if n/a */
    unsigned int memory_ok_samples;
    unsigned int memory_pressure_samples; /* available < 20% */

    WT_ProcessInfo top_processes[WT_MAX_TOP_PROCESSES];
    size_t top_process_count;

    WT_ShortLivedProcess short_lived[WT_MAX_SHORT_LIVED];
    size_t short_lived_count;

    WT_PowerInfo power;

    WT_BootReport boot;

    WT_UpdateStatus updates;

    WT_GpuMetrics gpu;

    WT_AudioReport audio;

    WT_SearchReport search;

    WT_DefenderReport defender;

    WT_NetTotals network;

    unsigned int scan_sample_count;
    unsigned int scan_sample_interval_ms;
    unsigned int collector_ms[WT_COLLECTOR_COUNT];

    int os_ok;
    int cpu_ok;
    int memory_ok;
    int disk_ok;
    int disk_active_ok;
    int disk_throughput_ok;
    int disk_queue_ok;
    int disk_latency_ok;
    int processes_ok;
    int power_ok;
    int boot_ok;
    int updates_ok;
    int gpu_ok;
    int audio_ok;
    int search_ok;
    int defender_ok;
    int network_ok;
} WT_ScanReport;

/* Zero-initializes a report and clears all availability flags. */
WT_Result wt_scan_report_init(WT_ScanReport *report);

#endif /* WINTUNE_REPORT_MODEL_H */
