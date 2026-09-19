#include "core/confidence.h"

#include <stdio.h>
#include <string.h>

const char *wt_confidence_metric_name(WT_ConfMetric metric)
{
    switch (metric) {
    case WT_CONF_SNAPSHOT:
        return "snapshot";
    case WT_CONF_CPU:
        return "cpu";
    case WT_CONF_MEMORY:
        return "memory";
    case WT_CONF_DISK:
        return "disk";
    case WT_CONF_GPU:
        return "gpu";
    case WT_CONF_BOOT:
        return "boot";
    default:
        return "unknown";
    }
}

static unsigned int wt_conf_ok_samples(const WT_ScanReport *rep,
                                       WT_ConfMetric metric)
{
    if (rep == NULL) {
        return 0;
    }
    switch (metric) {
    case WT_CONF_CPU:
        return rep->cpu_ok_samples > 0 ? rep->cpu_ok_samples
                                       : (rep->cpu_ok ? 1u : 0u);
    case WT_CONF_MEMORY:
        return rep->memory_ok_samples > 0 ? rep->memory_ok_samples
                                          : (rep->memory_ok ? 1u : 0u);
    case WT_CONF_DISK:
        return rep->disk_active_ok_samples > 0
                   ? rep->disk_active_ok_samples
                   : (rep->disk_active_ok ? 1u : 0u);
    case WT_CONF_GPU:
        return rep->scan_sample_count > 0 ? rep->scan_sample_count : 1u;
    case WT_CONF_BOOT:
        return rep->boot.history.count > 0
                   ? (unsigned int)rep->boot.history.count
                   : (rep->boot_ok ? 1u : 0u);
    case WT_CONF_SNAPSHOT:
    default:
        return 1u;
    }
}

static double wt_conf_spread(const WT_ScanReport *rep, WT_ConfMetric metric)
{
    /* Peak − average as a crude variance proxy (percentage points). */
    if (rep == NULL) {
        return 0.0;
    }
    switch (metric) {
    case WT_CONF_CPU:
        if (rep->cpu_max_percent >= 0.0 && rep->cpu_ok) {
            double d = rep->cpu_max_percent - rep->cpu.total_usage_percent;
            return d > 0.0 ? d : 0.0;
        }
        break;
    case WT_CONF_DISK:
        if (rep->disk_active_max_percent >= 0.0 && rep->disk_active_ok) {
            double d =
                rep->disk_active_max_percent - rep->disk_active_percent;
            return d > 0.0 ? d : 0.0;
        }
        break;
    case WT_CONF_MEMORY:
        if (rep->memory_max_used_percent >= 0.0 && rep->memory_ok) {
            double d =
                rep->memory_max_used_percent - rep->memory.used_percent;
            return d > 0.0 ? d : 0.0;
        }
        break;
    default:
        break;
    }
    return 0.0;
}

static unsigned long wt_conf_window_ms(const WT_ScanReport *rep)
{
    unsigned int n;
    unsigned int interval;
    if (rep == NULL) {
        return 0;
    }
    n = rep->scan_sample_count;
    interval = rep->scan_sample_interval_ms;
    if (n <= 1u || interval == 0u) {
        return 0;
    }
    return (unsigned long)(n - 1u) * (unsigned long)interval;
}

int wt_confidence_compute(const WT_ScanReport *report, WT_ConfMetric metric,
                          int base)
{
    int conf = base;
    unsigned int ok;
    unsigned long window_ms;
    double spread;

    if (base < 40) {
        base = 40;
    }
    if (base > 95) {
        base = 95;
    }
    conf = base;

    if (metric == WT_CONF_SNAPSHOT) {
        return conf;
    }

    ok = wt_conf_ok_samples(report, metric);
    window_ms = wt_conf_window_ms(report);
    spread = wt_conf_spread(report, metric);

    if (ok <= 1u) {
        conf -= 15; /* single-sample penalty for volatile metrics */
    } else if (ok >= 5u) {
        conf += 8;
    } else if (ok >= 3u) {
        conf += 5;
    } else {
        conf += 2; /* exactly 2 samples */
    }

    if (window_ms >= 8000ul) {
        conf += 8;
    } else if (window_ms >= 3000ul) {
        conf += 5;
    } else if (window_ms >= 1000ul) {
        conf += 2;
    }

    if (spread > 40.0) {
        conf -= 12;
    } else if (spread > 25.0) {
        conf -= 8;
    } else if (spread > 15.0) {
        conf -= 3;
    }

    if (metric == WT_CONF_DISK && report != NULL) {
        unsigned int hot = report->disk_active_hot_samples;
        if (ok > 1u && (hot * 2u) > ok) {
            conf += 5;
        }
    }
    if (metric == WT_CONF_CPU && report != NULL) {
        unsigned int hot = report->cpu_hot_samples;
        if (ok > 1u && (hot * 2u) > ok) {
            conf += 5;
        }
    }
    if (metric == WT_CONF_BOOT && report != NULL) {
        if (report->boot.history.count >= 3) {
            conf += 5;
        } else if (report->boot.history.count <= 1) {
            conf -= 10;
        }
    }

    if (conf > 95) {
        conf = 95;
    }
    if (conf < 40) {
        conf = 40;
    }
    return conf;
}

int wt_confidence_should_emit(const WT_ScanReport *report, WT_ConfMetric metric,
                              int confidence)
{
    unsigned int ok;
    unsigned int samples;

    if (metric == WT_CONF_SNAPSHOT || metric == WT_CONF_BOOT) {
        return confidence >= WT_CONF_EMIT_MIN;
    }

    samples = (report != NULL && report->scan_sample_count > 0)
                  ? report->scan_sample_count
                  : 1u;
    ok = wt_conf_ok_samples(report, metric);

    /* Suppress noisy single-sample volatile recommendations. */
    if (samples <= 1u || ok <= 1u) {
        if (metric == WT_CONF_CPU && report != NULL && report->cpu_ok &&
            report->cpu.total_usage_percent >= 95.0) {
            return confidence >= WT_CONF_EMIT_MIN;
        }
        if (metric == WT_CONF_DISK && report != NULL && report->disk_active_ok &&
            report->disk_active_percent >= 95.0) {
            return confidence >= WT_CONF_EMIT_MIN;
        }
        if (metric == WT_CONF_MEMORY && report != NULL && report->memory_ok) {
            /* Memory pressure is relatively stable even on one read. */
            return confidence >= WT_CONF_EMIT_MIN;
        }
        /* GPU and mild CPU/disk single samples: suppress. */
        return 0;
    }

    return confidence >= WT_CONF_EMIT_MIN;
}

void wt_confidence_basis(const WT_ScanReport *report, WT_ConfMetric metric,
                         char *out, size_t out_cap)
{
    unsigned int ok;
    unsigned long window_ms;
    double spread;

    if (out == NULL || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (report == NULL) {
        return;
    }

    ok = wt_conf_ok_samples(report, metric);
    window_ms = wt_conf_window_ms(report);
    spread = wt_conf_spread(report, metric);

    if (metric == WT_CONF_SNAPSHOT) {
        snprintf(out, out_cap, "snapshot (not sample-averaged)");
        return;
    }
    if (metric == WT_CONF_BOOT) {
        snprintf(out, out_cap, "%u boot history sample(s)", ok);
        return;
    }

    if (ok <= 1u) {
        snprintf(out, out_cap, "single sample");
    } else if (window_ms > 0) {
        snprintf(out, out_cap, "%u samples over %.1fs", ok,
                 window_ms / 1000.0);
    } else {
        snprintf(out, out_cap, "%u samples", ok);
    }

    if (spread > 15.0 && out_cap > strlen(out) + 24) {
        size_t n = strlen(out);
        snprintf(out + n, out_cap - n, "; spread %.0f pp", spread);
    }
}
