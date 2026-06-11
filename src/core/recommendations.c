#include "core/recommendations.h"
#include "system/power.h"

#include <stdio.h>
#include <string.h>

/* Thresholds are intentionally simple and explicit so recommendations are
 * deterministic and easy to justify. */
#define WT_MEM_AVAIL_HIGH_PCT    10.0  /* < this available => HIGH severity */
#define WT_MEM_AVAIL_MED_PCT     20.0  /* < this available => MEDIUM severity */
#define WT_DISK_ACTIVE_PCT       90.0
#define WT_DISK_FREE_HIGH_PCT    5.0
#define WT_DISK_FREE_MED_PCT     10.0
#define WT_CPU_BUSY_PCT          85.0

const char *wt_severity_to_string(WT_Severity severity)
{
    switch (severity) {
    case WT_SEVERITY_INFO:     return "info";
    case WT_SEVERITY_LOW:      return "low";
    case WT_SEVERITY_MEDIUM:   return "medium";
    case WT_SEVERITY_HIGH:     return "high";
    case WT_SEVERITY_CRITICAL: return "critical";
    default:                   return "info";
    }
}

const char *wt_risk_to_string(WT_Risk risk)
{
    switch (risk) {
    case WT_RISK_NONE:   return "none";
    case WT_RISK_LOW:    return "low";
    case WT_RISK_MEDIUM: return "medium";
    case WT_RISK_HIGH:   return "high";
    default:             return "none";
    }
}

static WT_Recommendation *wt_rec_add(WT_RecommendationList *list)
{
    if (list->count >= WT_MAX_RECOMMENDATIONS) {
        return NULL;
    }
    WT_Recommendation *r = &list->items[list->count++];
    memset(r, 0, sizeof(*r));
    r->severity = WT_SEVERITY_INFO;
    r->risk = WT_RISK_NONE;
    r->confidence_percent = 80;
    return r;
}

static void wt_str_set(char *dst, size_t cap, const char *src)
{
    strncpy_s(dst, cap, src, _TRUNCATE);
}

static void wt_check_power(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (!rep->power_ok) {
        return;
    }
    const WT_PowerInfo *p = &rep->power;

    if (p->on_ac == 1 &&
        p->scheme != WT_POWER_HIGH_PERF && p->scheme != WT_POWER_ULTIMATE) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) return;
        wt_str_set(r->id, sizeof(r->id), "WT-POWER-001");
        wt_str_set(r->title, sizeof(r->title),
                   "Use a higher-performance power plan while plugged in");
        snprintf(r->reason, sizeof(r->reason),
                 "On AC power with the '%s' plan. While plugged in, the "
                 "High performance plan can improve responsiveness for "
                 "compiling, gaming, or other heavy workloads.",
                 wt_power_scheme_name(p->scheme));
        wt_str_set(r->action, sizeof(r->action), "wintune power --set performance");
        r->severity = (p->scheme == WT_POWER_POWER_SAVER)
                          ? WT_SEVERITY_MEDIUM : WT_SEVERITY_LOW;
        r->risk = WT_RISK_LOW;
        r->requires_admin = 0;
        r->rollback_available = 1;
        r->confidence_percent = 85;
    } else if (p->on_ac == 0 &&
               (p->scheme == WT_POWER_HIGH_PERF || p->scheme == WT_POWER_ULTIMATE)) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) return;
        wt_str_set(r->id, sizeof(r->id), "WT-POWER-002");
        wt_str_set(r->title, sizeof(r->title),
                   "Consider a balanced power plan on battery");
        snprintf(r->reason, sizeof(r->reason),
                 "Running on battery with the '%s' plan. A Balanced plan can "
                 "extend battery life with little impact on everyday tasks.",
                 wt_power_scheme_name(p->scheme));
        wt_str_set(r->action, sizeof(r->action), "wintune power --set balanced");
        r->severity = WT_SEVERITY_LOW;
        r->risk = WT_RISK_LOW;
        r->requires_admin = 0;
        r->rollback_available = 1;
        r->confidence_percent = 80;
    }
}

static void wt_check_memory(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (!rep->memory_ok || rep->memory.total_physical_bytes == 0) {
        return;
    }
    double avail_pct = (double)rep->memory.available_physical_bytes * 100.0 /
                       (double)rep->memory.total_physical_bytes;
    if (avail_pct >= WT_MEM_AVAIL_MED_PCT) {
        return;
    }

    WT_Recommendation *r = wt_rec_add(out);
    if (r == NULL) return;
    wt_str_set(r->id, sizeof(r->id), "WT-MEMORY-001");
    wt_str_set(r->title, sizeof(r->title), "Physical memory is running low");
    snprintf(r->reason, sizeof(r->reason),
             "Only %.1f%% of physical memory is available (%.1f%% used). Low "
             "available memory can cause paging and slowdowns.",
             avail_pct, rep->memory.used_percent);
    wt_str_set(r->action, sizeof(r->action),
               "Close or delay memory-heavy apps; review 'wintune top'.");
    r->severity = (avail_pct < WT_MEM_AVAIL_HIGH_PCT)
                      ? WT_SEVERITY_HIGH : WT_SEVERITY_MEDIUM;
    r->risk = WT_RISK_NONE;
    r->requires_admin = 0;
    r->rollback_available = 0;
    r->confidence_percent = 90;
}

static void wt_check_disk(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (rep->disk_active_ok && rep->disk_active_percent >= WT_DISK_ACTIVE_PCT) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-DISK-001");
            wt_str_set(r->title, sizeof(r->title), "Disk activity is very high");
            snprintf(r->reason, sizeof(r->reason),
                     "Physical disk active time was %.0f%% during the sample. "
                     "Sustained high disk usage often comes from antivirus "
                     "scans, search indexing, or backups.",
                     rep->disk_active_percent);
            wt_str_set(r->action, sizeof(r->action),
                       "Review disk-heavy processes; let scans/indexing finish.");
            r->severity = WT_SEVERITY_MEDIUM;
            r->risk = WT_RISK_NONE;
            r->confidence_percent = 70; /* single sample */
        }
    }

    if (rep->disk_ok) {
        /* Flag the most-constrained volume below the threshold. */
        const WT_DiskVolumeMetrics *worst = NULL;
        for (size_t i = 0; i < rep->volume_count; ++i) {
            const WT_DiskVolumeMetrics *v = &rep->volumes[i];
            if (v->free_percent < WT_DISK_FREE_MED_PCT &&
                (worst == NULL || v->free_percent < worst->free_percent)) {
                worst = v;
            }
        }
        if (worst != NULL) {
            WT_Recommendation *r = wt_rec_add(out);
            if (r != NULL) {
                wt_str_set(r->id, sizeof(r->id), "WT-DISK-002");
                wt_str_set(r->title, sizeof(r->title), "A volume is low on free space");
                snprintf(r->reason, sizeof(r->reason),
                         "Drive %c: has only %.1f%% free space. Very low free "
                         "space can slow down Windows and prevent updates.",
                         (char)worst->root_path[0], worst->free_percent);
                wt_str_set(r->action, sizeof(r->action),
                           "Free up space or move files to another volume.");
                r->severity = (worst->free_percent < WT_DISK_FREE_HIGH_PCT)
                                  ? WT_SEVERITY_HIGH : WT_SEVERITY_MEDIUM;
                r->risk = WT_RISK_NONE;
                r->confidence_percent = 95;
            }
        }
    }
}

static void wt_check_cpu(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (rep->cpu_ok && rep->cpu.available &&
        rep->cpu.total_usage_percent >= WT_CPU_BUSY_PCT) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) return;
        wt_str_set(r->id, sizeof(r->id), "WT-CPU-001");
        wt_str_set(r->title, sizeof(r->title), "CPU usage is high");
        snprintf(r->reason, sizeof(r->reason),
                 "Total CPU usage was %.1f%% during the sample. This is normal "
                 "under active workloads but worth reviewing if unexpected.",
                 rep->cpu.total_usage_percent);
        wt_str_set(r->action, sizeof(r->action),
                   "Review top CPU consumers with 'wintune top'.");
        r->severity = WT_SEVERITY_INFO;
        r->risk = WT_RISK_NONE;
        r->confidence_percent = 75;
    }
}

WT_Result wt_generate_recommendations(const WT_ScanReport *report,
                                      WT_RecommendationList *out)
{
    if (report == NULL || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out->count = 0;

    wt_check_power(report, out);
    wt_check_memory(report, out);
    wt_check_disk(report, out);
    wt_check_cpu(report, out);

    return WT_OK;
}
