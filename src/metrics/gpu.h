#ifndef WINTUNE_GPU_H
#define WINTUNE_GPU_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

#define WT_MAX_GPU_ADAPTERS 8
#define WT_GPU_NAME_MAX 128
#define WT_GPU_PROCS_PER_ADAPTER 8
#define WT_GPU_PROC_NAME_MAX 64

/* One process's share of a single adapter's GPU engines during the sample. */
typedef struct WT_GpuProcessUse {
    unsigned long pid;
    wchar_t name[WT_GPU_PROC_NAME_MAX];
    double engine_percent; /* sum of that PID's engines on this adapter, 0–100 */
} WT_GpuProcessUse;

typedef struct WT_GpuAdapter {
    wchar_t name[WT_GPU_NAME_MAX];
    unsigned long long dedicated_bytes;
    unsigned long long shared_bytes;
    double utilization_percent; /* -1 when PDH unavailable */
    int utilization_ok;
    unsigned long luid_high;
    unsigned long luid_low;
    WT_GpuProcessUse processes[WT_GPU_PROCS_PER_ADAPTER];
    size_t process_count;
} WT_GpuAdapter;

typedef struct WT_DisplayInfo {
    unsigned int display_count; /* active attached displays */
    unsigned int primary_width;
    unsigned int primary_height;
    int available;
} WT_DisplayInfo;

typedef struct WT_GpuMetrics {
    WT_GpuAdapter adapters[WT_MAX_GPU_ADAPTERS];
    size_t adapter_count;
    double max_utilization_percent; /* hottest adapter; -1 if n/a */
    int utilization_ok;
    WT_DisplayInfo display;
    int adapters_ok;
} WT_GpuMetrics;

/* Enumerates hardware DXGI adapters and optionally samples
 * \GPU Engine(*)\Utilization Percentage (PDH) over sample_ms.
 * Each adapter also keeps the busiest processes on that GPU (engine time
 * during the sample). Soft-fails when counters are missing (VMs / older builds).
 * Read-only: no driver changes, overclock, or FPS tuning. */
WT_Result wt_collect_gpu_metrics(unsigned int sample_ms, WT_GpuMetrics *out);

#endif /* WINTUNE_GPU_H */
