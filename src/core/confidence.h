#ifndef WINTUNE_CONFIDENCE_H
#define WINTUNE_CONFIDENCE_H

#include "core/report_model.h"

/* Phase 35 — recommendation confidence from sample count, variance, duration. */

typedef enum WT_ConfMetric {
    WT_CONF_SNAPSHOT = 0, /* power / updates / blockers — not sample-volatile */
    WT_CONF_CPU,
    WT_CONF_MEMORY,
    WT_CONF_DISK,
    WT_CONF_GPU,
    WT_CONF_BOOT
} WT_ConfMetric;

/* Minimum confidence to emit a sample-volatile recommendation. */
#define WT_CONF_EMIT_MIN 55

/* Compute 0–95 confidence. `base` is the metric-specific starting point. */
int wt_confidence_compute(const WT_ScanReport *report, WT_ConfMetric metric,
                          int base);

/* 0 = suppress (too noisy / single-sample without extreme signal). */
int wt_confidence_should_emit(const WT_ScanReport *report, WT_ConfMetric metric,
                              int confidence);

/* Short human basis string for text/JSON (e.g. "5 samples / 4.0s window"). */
void wt_confidence_basis(const WT_ScanReport *report, WT_ConfMetric metric,
                         char *out, size_t out_cap);

const char *wt_confidence_metric_name(WT_ConfMetric metric);

#endif /* WINTUNE_CONFIDENCE_H */
