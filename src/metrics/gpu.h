#ifndef WINTUNE_GPU_H
#define WINTUNE_GPU_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

#define WT_MAX_GPU_ADAPTERS 8
#define WT_GPU_NAME_MAX 128

typedef struct WT_GpuAdapter {
    wchar_t name[WT_GPU_NAME_MAX];
    unsigned long long dedicated_bytes;
    unsigned long long shared_bytes;
    double utilization_percent; /* -1 when PDH unavailable */
    int utilization_ok;
    unsigned long luid_high;
    unsigned long luid_low;
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
 * Soft-fails when counters are missing (VMs / older builds).
 * Read-only: no driver changes, overclock, or FPS tuning. */
WT_Result wt_collect_gpu_metrics(unsigned int sample_ms, WT_GpuMetrics *out);

#endif /* WINTUNE_GPU_H */
