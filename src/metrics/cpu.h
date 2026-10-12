#ifndef WINTUNE_CPU_H
#define WINTUNE_CPU_H

#include "common/error.h"

typedef struct WT_CpuMetrics {
    double total_usage_percent;          /* -1.0 when not measured */
    unsigned int logical_processor_count;
    int available;                       /* 1 when total_usage_percent is valid */
    double dpc_percent;                  /* -1.0 when not measured */
    double interrupt_percent;            /* -1.0 when not measured */
    int kernel_source_ok;                /* 1 when a loud DPC or interrupt was named */
    int kernel_source_is_dpc;            /* 1 = DPC, 0 = interrupt */
    char kernel_source[64];              /* driver or image file name only */
} WT_CpuMetrics;

/* Collects total CPU usage via PDH. `sample_ms` is the interval between the two
 * PDH collections (0 selects a sensible default). The logical processor count
 * is always populated even if the PDH sample fails. DPC and interrupt percents
 * share that same sample window. */
WT_Result wt_collect_cpu_metrics(unsigned int sample_ms, WT_CpuMetrics *out);

/* When total CPU is high, DPC plus interrupt time is a real share of it, and
 * user processes do not explain the usage, names the loudest DPC or interrupt
 * image. `user_cpu_max` and `user_cpu_sum` are machine-wide percents; pass a
 * negative value when they were not measured. Read-only. A missing privilege
 * leaves the name unset. Does not run unless the condition holds. */
void wt_cpu_attribute_kernel_source(WT_CpuMetrics *cpu,
                                    double user_cpu_max,
                                    double user_cpu_sum);

#endif /* WINTUNE_CPU_H */
