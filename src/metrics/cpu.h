#ifndef WINTUNE_CPU_H
#define WINTUNE_CPU_H

#include "common/error.h"

typedef struct WT_CpuMetrics {
    double total_usage_percent;          /* -1.0 when not measured */
    unsigned int logical_processor_count;
    int available;                       /* 1 when total_usage_percent is valid */
} WT_CpuMetrics;

/* Collects total CPU usage via PDH. `sample_ms` is the interval between the two
 * PDH collections (0 selects a sensible default). The logical processor count
 * is always populated even if the PDH sample fails. */
WT_Result wt_collect_cpu_metrics(unsigned int sample_ms, WT_CpuMetrics *out);

#endif /* WINTUNE_CPU_H */
