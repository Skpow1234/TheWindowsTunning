#ifndef WINTUNE_MEMORY_H
#define WINTUNE_MEMORY_H

#include "common/error.h"

typedef struct WT_MemoryMetrics {
    unsigned long long total_physical_bytes;
    unsigned long long available_physical_bytes;
    unsigned long long used_physical_bytes;
    double used_percent;

    /* Phase 50: commit charge (GetPerformanceInfo). */
    int commit_ok;
    unsigned long long commit_total_bytes;
    unsigned long long commit_limit_bytes;
    unsigned long long commit_peak_bytes;
    double commit_percent; /* total/limit * 100; 0 if limit unknown */

    /* Commit headroom from GlobalMemoryStatusEx. */
    int pagefile_ok;
    unsigned long long commit_available_bytes; /* ullAvailPageFile */

    /* Optional PDH rates (filled by wt_collect_memory_metrics_ex). */
    int hard_faults_ok;
    double hard_faults_per_sec; /* \\Memory\\Pages Input/sec; -1 if n/a */
    int page_faults_ok;
    double page_faults_per_sec; /* \\Memory\\Page Faults/sec; -1 if n/a */
} WT_MemoryMetrics;

/* Collects physical + commit metrics (no blocking PDH sample). */
WT_Result wt_collect_memory_metrics(WT_MemoryMetrics *out);

/* Same as above, then samples page-fault PDH counters when fault_sample_ms > 0. */
WT_Result wt_collect_memory_metrics_ex(WT_MemoryMetrics *out,
                                       unsigned int fault_sample_ms);

#endif /* WINTUNE_MEMORY_H */
