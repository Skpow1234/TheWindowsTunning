#ifndef WINTUNE_MEMORY_H
#define WINTUNE_MEMORY_H

#include "common/error.h"

typedef struct WT_MemoryMetrics {
    unsigned long long total_physical_bytes;
    unsigned long long available_physical_bytes;
    unsigned long long used_physical_bytes;
    double used_percent;
} WT_MemoryMetrics;

/* Collects system-wide physical memory metrics via GlobalMemoryStatusEx. */
WT_Result wt_collect_memory_metrics(WT_MemoryMetrics *out);

#endif /* WINTUNE_MEMORY_H */
