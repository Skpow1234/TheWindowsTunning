#include "core/recommendations.h"
#include "core/confidence.h"
#include "core/impact_score.h"
#include "system/power.h"
#include "system/boot.h"
#include "system/updates.h"
#include "system/blockers.h"
#include "system/startup.h"
#include "system/tasks.h"
#include "system/file_identity.h"
#include "system/uninstall.h"
#include "system/storage_health.h"
#include "system/reliability.h"
#include "system/maintenance.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Thresholds are intentionally simple and explicit so recommendations are
 * deterministic and easy to justify. */
#define WT_MEM_AVAIL_HIGH_PCT    10.0  /* < this available => HIGH severity */
#define WT_MEM_AVAIL_MED_PCT     20.0  /* < this available => MEDIUM severity */
#define WT_MEM_COMMIT_HIGH_PCT   90.0
#define WT_MEM_COMMIT_MED_PCT    85.0
#define WT_MEM_HARD_FAULT_HIGH   200.0 /* Pages Input/sec */
#define WT_DISK_ACTIVE_PCT       90.0
#define WT_DISK_FREE_HIGH_PCT    5.0
#define WT_DISK_FREE_MED_PCT     10.0
#define WT_DISK_THRU_HIGH_BPS    (50.0 * 1024.0 * 1024.0) /* 50 MB/s */
#define WT_DISK_THRU_MED_BPS     (20.0 * 1024.0 * 1024.0)
#define WT_DISK_QUEUE_HIGH       4.0
#define WT_CPU_BUSY_PCT          85.0
#define WT_BOOT_SLOW_MS          60000u
#define WT_BOOT_APP_SLOW_MS      3000u

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

static void wt_rec_set_confidence(WT_Recommendation *r, const WT_ScanReport *rep,
                                  WT_ConfMetric metric, int base)
{
    if (r == NULL) {
        return;
    }
    r->confidence_percent = wt_confidence_compute(rep, metric, base);
    wt_confidence_basis(rep, metric, r->confidence_basis,
                        sizeof(r->confidence_basis));
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
        wt_str_set(r->action, sizeof(r->action), "wintune apply WT-POWER-001");
        r->severity = (p->scheme == WT_POWER_POWER_SAVER)
                          ? WT_SEVERITY_MEDIUM : WT_SEVERITY_LOW;
        r->risk = WT_RISK_LOW;
        r->requires_admin = 0;
        r->rollback_available = 1;
        wt_rec_set_confidence(r, rep, WT_CONF_SNAPSHOT, 85);
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
        wt_str_set(r->action, sizeof(r->action), "wintune apply WT-POWER-002");
        r->severity = WT_SEVERITY_LOW;
        r->risk = WT_RISK_LOW;
        r->requires_admin = 0;
        r->rollback_available = 1;
        wt_rec_set_confidence(r, rep, WT_CONF_SNAPSHOT, 80);
    }

    /* Phase 29: plan may intentionally limit processor max state. */
    if (p->processor_capped) {
        int cap = (p->on_ac == 1) ? p->processor_max_pct_ac
                                  : p->processor_max_pct_dc;
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-POWER-003");
            wt_str_set(r->title, sizeof(r->title),
                       "Current power plan caps processor performance");
            snprintf(r->reason, sizeof(r->reason),
                     "The active '%s' plan sets processor maximum state to "
                     "%d%% for the current power source (%s). That can make "
                     "the machine feel slower under load. Switching to "
                     "Balanced or High performance (when plugged in) raises "
                     "the cap using an existing Windows plan.",
                     wt_power_scheme_name(p->scheme),
                     cap >= 0 ? cap : 0,
                     p->on_ac == 1 ? "AC" : "battery");
            if (p->on_ac == 1) {
                wt_str_set(r->action, sizeof(r->action),
                           "wintune apply WT-POWER-001");
                r->rollback_available = 1;
            } else {
                wt_str_set(r->action, sizeof(r->action),
                           "Review power plan; use Balanced if on High performance.");
                r->rollback_available = 0;
            }
            r->severity = WT_SEVERITY_INFO;
            r->risk = WT_RISK_LOW;
            r->requires_admin = 0;
            wt_rec_set_confidence(r, rep, WT_CONF_SNAPSHOT, 80);
        }
    }

    /* High discharge rate while on battery (signed mW; negative = drain). */
    if (p->on_ac == 0 && p->rate_ok && p->discharging == 1 &&
        p->rate_mw <= -20000) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            double watts = (-(double)p->rate_mw) / 1000.0;
            wt_str_set(r->id, sizeof(r->id), "WT-POWER-004");
            wt_str_set(r->title, sizeof(r->title),
                       "Battery is discharging quickly");
            if (p->estimated_seconds > 0) {
                snprintf(r->reason, sizeof(r->reason),
                         "Battery discharge is about %.1f W. Estimated time "
                         "remaining at this rate is roughly %d minutes. Heavy "
                         "CPU/GPU work, bright displays, or High performance "
                         "plans increase drain. WinTune does not change fans "
                         "or firmware.",
                         watts, p->estimated_seconds / 60);
            } else {
                snprintf(r->reason, sizeof(r->reason),
                         "Battery discharge is about %.1f W. Heavy CPU/GPU "
                         "work or a High performance plan can increase drain. "
                         "WinTune does not change fans or firmware.",
                         watts);
            }
            wt_str_set(r->action, sizeof(r->action),
                       "Reduce load or switch to Balanced/Power saver on battery.");
            r->severity = WT_SEVERITY_LOW;
            r->risk = WT_RISK_NONE;
            r->requires_admin = 0;
            r->rollback_available = 0;
            wt_rec_set_confidence(r, rep, WT_CONF_SNAPSHOT, 70);
        }
    }
}

static void wt_check_memory(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    int conf;
    if (!rep->memory_ok || rep->memory.total_physical_bytes == 0) {
        return;
    }
    double avail_pct = (double)rep->memory.available_physical_bytes * 100.0 /
                       (double)rep->memory.total_physical_bytes;
    if (avail_pct >= WT_MEM_AVAIL_MED_PCT) {
        return;
    }

    /* Multi-sample: prefer sustained pressure when we have sample stats. */
    if (rep->memory_ok_samples > 1u &&
        rep->memory_pressure_samples * 2u <= rep->memory_ok_samples &&
        avail_pct >= WT_MEM_AVAIL_HIGH_PCT) {
        return;
    }

    conf = wt_confidence_compute(rep, WT_CONF_MEMORY, 90);
    if (!wt_confidence_should_emit(rep, WT_CONF_MEMORY, conf)) {
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
               "Close or delay memory-heavy apps; review 'wintune top' / "
               "'wintune memory'.");
    r->severity = (avail_pct < WT_MEM_AVAIL_HIGH_PCT)
                      ? WT_SEVERITY_HIGH : WT_SEVERITY_MEDIUM;
    r->risk = WT_RISK_NONE;
    r->requires_admin = 0;
    r->rollback_available = 0;
    wt_rec_set_confidence(r, rep, WT_CONF_MEMORY, 90);
}

static void wt_check_memory_commit(const WT_ScanReport *rep,
                                   WT_RecommendationList *out)
{
    WT_Recommendation *r;
    double commit_pct;

    if (!rep->memory_ok || !rep->memory.commit_ok ||
        rep->memory.commit_limit_bytes == 0) {
        return;
    }
    commit_pct = rep->memory.commit_percent;
    if (commit_pct < WT_MEM_COMMIT_MED_PCT) {
        return;
    }

    r = wt_rec_add(out);
    if (r == NULL) {
        return;
    }
    wt_str_set(r->id, sizeof(r->id), "WT-MEMORY-002");
    wt_str_set(r->title, sizeof(r->title), "Commit charge is high");
    snprintf(r->reason, sizeof(r->reason),
             "System commit charge is %.1f%% of the commit limit "
             "(peak was also tracked). High commit means Windows may page "
             "heavily; this is not fixed by \"RAM cleaners\".",
             commit_pct);
    wt_str_set(r->action, sizeof(r->action),
               "Close or delay commit-heavy apps; review 'wintune memory' and "
               "'wintune top'. Never empty working sets as an optimization.");
    r->severity = (commit_pct >= WT_MEM_COMMIT_HIGH_PCT) ? WT_SEVERITY_HIGH
                                                         : WT_SEVERITY_MEDIUM;
    r->risk = WT_RISK_NONE;
    r->requires_admin = 0;
    r->rollback_available = 0;
    r->confidence_percent = 88;
    wt_str_set(r->confidence_basis, sizeof(r->confidence_basis),
               "GetPerformanceInfo CommitTotal/Limit");
}

static void wt_check_memory_hard_faults(const WT_ScanReport *rep,
                                        WT_RecommendationList *out)
{
    WT_Recommendation *r;
    WT_MemoryMetrics m;
    double avail_pct = 100.0;

    if (!rep->memory_ok) {
        return;
    }
    m = rep->memory;
    if (!m.hard_faults_ok) {
        /* One short Pages Input/sec sample for recommend/doctor. */
        if (wt_collect_memory_metrics_ex(&m, 400) != WT_OK || !m.hard_faults_ok) {
            return;
        }
    }
    if (m.hard_faults_per_sec < WT_MEM_HARD_FAULT_HIGH) {
        return;
    }
    /* Only warn when also under some memory/commit pressure. */
    if (m.total_physical_bytes > 0) {
        avail_pct = (double)m.available_physical_bytes * 100.0 /
                    (double)m.total_physical_bytes;
    }
    if (m.commit_ok && m.commit_percent < WT_MEM_COMMIT_MED_PCT &&
        avail_pct >= WT_MEM_AVAIL_MED_PCT) {
        return;
    }

    r = wt_rec_add(out);
    if (r == NULL) {
        return;
    }
    wt_str_set(r->id, sizeof(r->id), "WT-MEMORY-003");
    wt_str_set(r->title, sizeof(r->title), "High hard-fault (paging) rate");
    snprintf(r->reason, sizeof(r->reason),
             "Memory Pages Input/sec is about %.0f. Sustained hard faults mean "
             "the disk is serving page traffic, which feels like system "
             "slowness.",
             m.hard_faults_per_sec);
    wt_str_set(r->action, sizeof(r->action),
               "Reduce concurrent heavy apps; wait for scans/backups to "
               "finish. Run: wintune memory. WinTune never trims working sets.");
    r->severity = WT_SEVERITY_MEDIUM;
    r->risk = WT_RISK_NONE;
    r->confidence_percent = 75;
    wt_str_set(r->confidence_basis, sizeof(r->confidence_basis),
               "PDH Memory\\Pages Input/sec");
}

static const WT_DiskVolumeMetrics *wt_hottest_volume(const WT_ScanReport *rep)
{
    const WT_DiskVolumeMetrics *hot = NULL;
    if (rep == NULL || !rep->disk_ok) {
        return NULL;
    }
    for (size_t i = 0; i < rep->volume_count; ++i) {
        const WT_DiskVolumeMetrics *v = &rep->volumes[i];
        if (!v->activity_ok) {
            continue;
        }
        if (hot == NULL || v->active_percent > hot->active_percent) {
            hot = v;
        }
    }
    return hot;
}

/* Phase 30: fire on sustained pressure, not a single spike.
 * Sustained = average >= threshold, or a strict majority of successful
 * samples were hot. Single-sample / unset counters keep prior behavior. */
static int wt_disk_sustained_hot(const WT_ScanReport *rep)
{
    unsigned int ok;
    unsigned int hot;

    if (!rep->disk_active_ok) {
        return 0;
    }

    ok = rep->disk_active_ok_samples;
    hot = rep->disk_active_hot_samples;
    if (ok == 0u) {
        return rep->disk_active_percent >= WT_DISK_ACTIVE_PCT;
    }
    if (rep->disk_active_percent >= WT_DISK_ACTIVE_PCT) {
        return 1;
    }
    return (hot * 2u) > ok;
}

static int wt_disk_active_confidence(const WT_ScanReport *rep, int base_single,
                                     int base_multi)
{
    int base = (rep->scan_sample_count <= 1 ||
                rep->disk_active_ok_samples <= 1u)
                   ? base_single
                   : base_multi;
    return wt_confidence_compute(rep, WT_CONF_DISK, base);
}

static void wt_disk_sample_suffix(const WT_ScanReport *rep, char *buf,
                                  size_t buf_sz)
{
    unsigned int ok = rep->disk_active_ok_samples;
    unsigned int hot = rep->disk_active_hot_samples;

    buf[0] = '\0';
    if (ok == 0u) {
        return;
    }
    if (rep->disk_active_max_percent >= 0.0 && ok > 1u) {
        snprintf(buf, buf_sz,
                 " Across %u samples: average %.0f%%, peak %.0f%%, %u of %u "
                 "above %.0f%%.",
                 ok, rep->disk_active_percent, rep->disk_active_max_percent,
                 hot, ok, WT_DISK_ACTIVE_PCT);
    } else if (ok > 1u) {
        snprintf(buf, buf_sz,
                 " Across %u samples: average %.0f%%, %u of %u above %.0f%%.",
                 ok, rep->disk_active_percent, hot, ok, WT_DISK_ACTIVE_PCT);
    }
}

static void wt_check_disk(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    double thru = 0.0;
    char sample_note[192];

    if (rep->disk_throughput_ok) {
        if (rep->disk_read_bytes_per_sec > 0.0) {
            thru += rep->disk_read_bytes_per_sec;
        }
        if (rep->disk_write_bytes_per_sec > 0.0) {
            thru += rep->disk_write_bytes_per_sec;
        }
    }

    const WT_DiskVolumeMetrics *hot = wt_hottest_volume(rep);
    int total_hot = wt_disk_sustained_hot(rep);
    int volume_hot =
        (hot != NULL && hot->activity_ok &&
         hot->active_percent >= WT_DISK_ACTIVE_PCT);

    wt_disk_sample_suffix(rep, sample_note, sizeof(sample_note));

    if (total_hot) {
        int dconf = wt_disk_active_confidence(rep, 70, 80);
        if (!wt_confidence_should_emit(rep, WT_CONF_DISK, dconf)) {
            total_hot = 0; /* suppress noisy single-sample disk alarm */
        }
    }

    if (total_hot) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-DISK-001");
            wt_str_set(r->title, sizeof(r->title), "Disk activity is very high");
            if (hot != NULL && hot->activity_ok &&
                rep->disk_throughput_ok && thru >= WT_DISK_THRU_MED_BPS) {
                snprintf(r->reason, sizeof(r->reason),
                         "Physical disk active time averaged %.0f%% (hottest "
                         "volume %c: at %.0f%%) with about %.1f MB/s combined "
                         "throughput (%.1f MB/s read, %.1f MB/s write)."
                         "%s Sustained load often comes from antivirus scans, "
                         "search indexing, or backups.",
                         rep->disk_active_percent, (char)hot->root_path[0],
                         hot->active_percent, thru / (1024.0 * 1024.0),
                         rep->disk_read_bytes_per_sec / (1024.0 * 1024.0),
                         rep->disk_write_bytes_per_sec / (1024.0 * 1024.0),
                         sample_note);
                r->confidence_percent =
                    wt_disk_active_confidence(rep, 75, 85);
            } else if (hot != NULL && hot->activity_ok) {
                snprintf(r->reason, sizeof(r->reason),
                         "Physical disk active time averaged %.0f%%; volume "
                         "%c: was about %.0f%% active."
                         "%s Sustained high disk usage often comes from "
                         "antivirus scans, search indexing, or backups.",
                         rep->disk_active_percent, (char)hot->root_path[0],
                         hot->active_percent, sample_note);
                r->confidence_percent =
                    wt_disk_active_confidence(rep, 70, 80);
            } else if (rep->disk_throughput_ok && thru >= WT_DISK_THRU_MED_BPS) {
                snprintf(r->reason, sizeof(r->reason),
                         "Physical disk active time averaged %.0f%% with about "
                         "%.1f MB/s combined throughput (%.1f MB/s read, "
                         "%.1f MB/s write)."
                         "%s Sustained load often comes from antivirus scans, "
                         "search indexing, or backups.",
                         rep->disk_active_percent,
                         thru / (1024.0 * 1024.0),
                         rep->disk_read_bytes_per_sec / (1024.0 * 1024.0),
                         rep->disk_write_bytes_per_sec / (1024.0 * 1024.0),
                         sample_note);
                r->confidence_percent =
                    wt_disk_active_confidence(rep, 75, 85);
            } else {
                snprintf(r->reason, sizeof(r->reason),
                         "Physical disk active time averaged %.0f%%."
                         "%s Sustained high disk usage often comes from "
                         "antivirus scans, search indexing, or backups.",
                         rep->disk_active_percent, sample_note);
                r->confidence_percent =
                    wt_disk_active_confidence(rep, 70, 80);
            }
            wt_confidence_basis(rep, WT_CONF_DISK, r->confidence_basis,
                                sizeof(r->confidence_basis));
            wt_str_set(r->action, sizeof(r->action),
                       "Review disk-heavy processes; let scans/indexing finish.");
            r->severity = WT_SEVERITY_MEDIUM;
            r->risk = WT_RISK_NONE;
        }
    } else if (volume_hot) {
        /* One volume is saturated while system total is not (Phase 26). */
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            double v_thru = 0.0;
            if (hot->throughput_ok) {
                if (hot->read_bytes_per_sec > 0.0) {
                    v_thru += hot->read_bytes_per_sec;
                }
                if (hot->write_bytes_per_sec > 0.0) {
                    v_thru += hot->write_bytes_per_sec;
                }
            }
            wt_str_set(r->id, sizeof(r->id), "WT-DISK-004");
            wt_str_set(r->title, sizeof(r->title),
                       "One volume has very high disk activity");
            if (hot->throughput_ok && v_thru >= WT_DISK_THRU_MED_BPS) {
                snprintf(r->reason, sizeof(r->reason),
                         "Drive %c: LogicalDisk active time was %.0f%% with "
                         "about %.1f MB/s combined throughput, while overall "
                         "PhysicalDisk(_Total) was not as elevated. Pressure "
                         "may be localized to this volume.",
                         (char)hot->root_path[0], hot->active_percent,
                         v_thru / (1024.0 * 1024.0));
            } else {
                snprintf(r->reason, sizeof(r->reason),
                         "Drive %c: LogicalDisk active time was %.0f%% during "
                         "the sample, while overall PhysicalDisk(_Total) was "
                         "not as elevated. Pressure may be localized to this "
                         "volume (backups, installs, or large file copies).",
                         (char)hot->root_path[0], hot->active_percent);
            }
            wt_str_set(r->action, sizeof(r->action),
                       "Review activity on that volume; check top disk I/O.");
            r->severity = WT_SEVERITY_MEDIUM;
            r->risk = WT_RISK_NONE;
            r->confidence_percent = (rep->scan_sample_count > 1) ? 80 : 70;
        }
    } else if (rep->disk_throughput_ok && thru >= WT_DISK_THRU_HIGH_BPS) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-DISK-003");
            wt_str_set(r->title, sizeof(r->title),
                       "Disk throughput is high");
            snprintf(r->reason, sizeof(r->reason),
                     "Physical disk throughput averaged about %.1f MB/s "
                     "(%.1f MB/s read, %.1f MB/s write) during the sample%s. "
                     "This is normal under heavy I/O but worth reviewing if "
                     "unexpected.",
                     thru / (1024.0 * 1024.0),
                     rep->disk_read_bytes_per_sec / (1024.0 * 1024.0),
                     rep->disk_write_bytes_per_sec / (1024.0 * 1024.0),
                     (rep->disk_queue_ok &&
                      rep->disk_avg_queue_length >= WT_DISK_QUEUE_HIGH)
                         ? "; queue length was also elevated"
                         : "");
            wt_str_set(r->action, sizeof(r->action),
                       "Review top disk consumers with 'wintune top --sort disk'.");
            r->severity = WT_SEVERITY_INFO;
            r->risk = WT_RISK_NONE;
            r->confidence_percent = (rep->scan_sample_count > 1) ? 80 : 70;
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

static void wt_check_storage_health(WT_RecommendationList *out)
{
    WT_StorageHealthReport health;
    WT_Result r = wt_collect_storage_health(&health);
    size_t i;

    if (r != WT_OK && health.disk_count == 0) {
        return;
    }
    if (!health.any_degraded) {
        return;
    }

    for (i = 0; i < health.disk_count; ++i) {
        const WT_StorageDiskHealth *d = &health.disks[i];
        WT_Recommendation *rec;
        if (d->status != WT_STORAGE_HEALTH_DEGRADED) {
            continue;
        }
        rec = wt_rec_add(out);
        if (rec == NULL) {
            break;
        }
        wt_str_set(rec->id, sizeof(rec->id), "WT-DISK-005");
        wt_str_set(rec->title, sizeof(rec->title),
                   "Storage failure prediction reported");
        {
            char model_utf8[128];
            model_utf8[0] = '\0';
            if (d->model[0] != L'\0') {
                (void)WideCharToMultiByte(CP_UTF8, 0, d->model, -1, model_utf8,
                                          (int)sizeof(model_utf8), NULL, NULL);
            }
            if (model_utf8[0] != '\0') {
                snprintf(rec->reason, sizeof(rec->reason),
                         "PhysicalDrive%u (%s) reported a predicted failure "
                         "via Windows storage health. Continuing to use a "
                         "failing disk risks data loss.",
                         d->physical_drive, model_utf8);
            } else {
                snprintf(rec->reason, sizeof(rec->reason),
                         "PhysicalDrive%u reported a predicted failure via "
                         "Windows storage health. Continuing to use a failing "
                         "disk risks data loss.",
                         d->physical_drive);
            }
        }
        wt_str_set(rec->action, sizeof(rec->action),
                   "Back up important data now. Run your disk vendor's "
                   "diagnostic tool. WinTune does not wipe, format, or repair "
                   "disks.");
        rec->severity = WT_SEVERITY_HIGH;
        rec->risk = WT_RISK_NONE;
        rec->requires_admin = 0;
        rec->rollback_available = 0;
        rec->confidence_percent = 90;
        wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                   "IOCTL_STORAGE_PREDICT_FAILURE");
        /* One advisory is enough even if multiple disks fail. */
        break;
    }
}

static void wt_check_reliability(WT_RecommendationList *out)
{
    WT_ReliabilityReport rel;
    WT_Result r = wt_collect_reliability(&rel);
    unsigned unexpected;
    WT_Recommendation *rec;
    int has_bugcheck = 0;
    int has_kernel_dump = 0;
    size_t i;

    if (r != WT_OK && !rel.events_ok && rel.dump_meta_count == 0) {
        return;
    }

    unexpected = rel.kernel_power_count + rel.unexpected_shutdown_count;
    if (unexpected >= 1u) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            wt_str_set(rec->id, sizeof(rec->id), "WT-RELIABILITY-001");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Recent unexpected shutdowns detected");
            snprintf(rec->reason, sizeof(rec->reason),
                     "Event Log shows %u unexpected power/shutdown signal(s) "
                     "in the last %d days (Kernel-Power 41 / Event 6008). "
                     "Systems often feel slow until a clean reboot after hard "
                     "power loss.",
                     unexpected, rel.lookback_days);
            wt_str_set(rec->action, sizeof(rec->action),
                       "Reboot cleanly when convenient. Check power/cables if "
                       "this repeats. Run: wintune reliability");
            rec->severity = (unexpected >= 2u) ? WT_SEVERITY_HIGH
                                               : WT_SEVERITY_MEDIUM;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 85;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                       "System Event Log 41/6008");
        }
    }

    if (rel.app_crash_count + rel.app_hang_count >= 3u) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            char top[96];
            top[0] = '\0';
            if (rel.crash_app_count > 0) {
                WideCharToMultiByte(CP_UTF8, 0, rel.crash_apps[0].name, -1, top,
                                    (int)sizeof(top), NULL, NULL);
            }
            wt_str_set(rec->id, sizeof(rec->id), "WT-RELIABILITY-002");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Repeated application crashes or hangs");
            if (top[0] != '\0') {
                snprintf(rec->reason, sizeof(rec->reason),
                         "%u app crash/hang event(s) in the last %d days "
                         "(top: %s x%u). Crash recovery can keep CPU and disk "
                         "busy.",
                         rel.app_crash_count + rel.app_hang_count,
                         rel.lookback_days, top, rel.crash_apps[0].count);
            } else {
                snprintf(rec->reason, sizeof(rec->reason),
                         "%u app crash/hang event(s) in the last %d days. "
                         "Crash recovery can keep CPU and disk busy.",
                         rel.app_crash_count + rel.app_hang_count,
                         rel.lookback_days);
            }
            wt_str_set(rec->action, sizeof(rec->action),
                       "Update or close noisy apps. Inspect with: "
                       "wintune reliability. WinTune does not repair apps.");
            rec->severity = WT_SEVERITY_MEDIUM;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 80;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                       "Application Event Log 1000/1002");
        }
    }

    if (rel.bugcheck_count >= 1u) {
        has_bugcheck = 1;
    }
    for (i = 0; i < rel.dump_meta_count; ++i) {
        if (_wcsicmp(rel.dumps[i].location, L"minidump") == 0 ||
            _wcsicmp(rel.dumps[i].location, L"memory.dmp") == 0) {
            has_kernel_dump = 1;
            break;
        }
    }
    if (has_bugcheck || has_kernel_dump) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            wt_str_set(rec->id, sizeof(rec->id), "WT-RELIABILITY-003");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Kernel crash / dump metadata present");
            snprintf(rec->reason, sizeof(rec->reason),
                     "Found %u bugcheck report(s)%s in the lookback window. "
                     "A recent bugcheck can explain lingering slowness.",
                     rel.bugcheck_count,
                     has_kernel_dump ? " and local kernel dump file(s)" : "");
            wt_str_set(rec->action, sizeof(rec->action),
                       "Review dumps with WinDbg or vendor tools if needed. "
                       "WinTune never opens or uploads dump contents. "
                       "Run: wintune reliability");
            rec->severity = WT_SEVERITY_HIGH;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 88;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
               "System 1001 / local dump metadata");
        }
    }
}

static void wt_check_maintenance(const WT_ScanReport *rep,
                                 WT_RecommendationList *out)
{
    WT_MaintenanceReport maint;
    WT_Recommendation *rec;

    if (wt_maintenance_from_scan(&maint, rep) != WT_OK) {
        return;
    }
    if (!maint.overlap) {
        return;
    }

    if (maint.defender_active && (maint.cpu_hot || maint.disk_hot)) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            wt_str_set(rec->id, sizeof(rec->id), "WT-MAINT-001");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Defender activity overlaps busy samples");
            snprintf(rec->reason, sizeof(rec->reason),
                     "High %s samples overlapped Microsoft Defender-related "
                     "processes or scheduled scans. Security scans are "
                     "important but can feel like system slowness.",
                     maint.disk_hot ? "disk" : "CPU");
            wt_str_set(rec->action, sizeof(rec->action),
                       "Prefer Windows Security scheduled scan outside work "
                       "hours / Active hours. Do not disable Defender. "
                       "Run: wintune maintenance");
            rec->severity = WT_SEVERITY_MEDIUM;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 82;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                       "scan hot samples + Defender process/task");
        }
    }

    if (maint.update_active && (maint.cpu_hot || maint.disk_hot)) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            wt_str_set(rec->id, sizeof(rec->id), "WT-MAINT-002");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Windows Update/servicing overlaps high load");
            wt_str_set(rec->reason, sizeof(rec->reason),
                       "Update Orchestrator / servicing workers were active "
                       "while CPU or disk samples were hot. Installing updates "
                       "during work hours can stall interactive use.");
            wt_str_set(rec->action, sizeof(rec->action),
                       "Schedule updates/reboots via Windows Update Active "
                       "hours. Do not disable Windows Update. "
                       "Run: wintune maintenance");
            rec->severity = WT_SEVERITY_MEDIUM;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 80;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                       "scan hot samples + WU/servicing process/task");
        }
    }

    if (maint.optimize_active && maint.disk_hot) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            wt_str_set(rec->id, sizeof(rec->id), "WT-MAINT-003");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Background optimization overlaps high disk");
            wt_str_set(rec->reason, sizeof(rec->reason),
                       "Search indexing, defrag/optimize, or compatibility "
                       "assessment overlapped high disk active time.");
            wt_str_set(rec->action, sizeof(rec->action),
                       "Review Task Scheduler / Storage optimize schedule for "
                       "off-peak hours. Do not disable security features. "
                       "Run: wintune maintenance");
            rec->severity = WT_SEVERITY_LOW;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 75;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                       "disk hot + optimization process/task");
        }
    }

    if ((maint.defender_active ? 1 : 0) + (maint.update_active ? 1 : 0) +
            (maint.optimize_active ? 1 : 0) >=
        2) {
        rec = wt_rec_add(out);
        if (rec != NULL) {
            wt_str_set(rec->id, sizeof(rec->id), "WT-MAINT-004");
            wt_str_set(rec->title, sizeof(rec->title),
                       "Multiple maintenance workloads concurrent");
            wt_str_set(rec->reason, sizeof(rec->reason),
                       "More than one maintenance class (Defender, Update, "
                       "optimization) was active during the scan window.");
            wt_str_set(rec->action, sizeof(rec->action),
                       "Stagger Defender scans, updates, and Optimize Drives "
                       "to idle hours when possible. Never disable security.");
            rec->severity = WT_SEVERITY_INFO;
            rec->risk = WT_RISK_NONE;
            rec->confidence_percent = 70;
            wt_str_set(rec->confidence_basis, sizeof(rec->confidence_basis),
                       "multiple maint kinds in one window");
        }
    }
}

static void wt_check_cpu(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    int conf;
    int sustained;

    if (!rep->cpu_ok || !rep->cpu.available ||
        rep->cpu.total_usage_percent < WT_CPU_BUSY_PCT) {
        return;
    }

    /* Prefer sustained hot CPU across samples when available. */
    sustained = 1;
    if (rep->cpu_ok_samples > 1u) {
        sustained = ((rep->cpu_hot_samples * 2u) > rep->cpu_ok_samples) ||
                    (rep->cpu.total_usage_percent >= WT_CPU_BUSY_PCT);
    }

    if (!sustained) {
        return;
    }

    conf = wt_confidence_compute(rep, WT_CONF_CPU, 75);
    if (!wt_confidence_should_emit(rep, WT_CONF_CPU, conf)) {
        return;
    }

    WT_Recommendation *r = wt_rec_add(out);
    if (r == NULL) {
        return;
    }
    wt_str_set(r->id, sizeof(r->id), "WT-CPU-001");
    wt_str_set(r->title, sizeof(r->title), "CPU usage is high");
    if (rep->cpu_ok_samples > 1u) {
        snprintf(r->reason, sizeof(r->reason),
                 "Total CPU usage averaged %.1f%% (peak %.1f%%; %u of %u "
                 "samples above %.0f%%). This is normal under active workloads "
                 "but worth reviewing if unexpected.",
                 rep->cpu.total_usage_percent,
                 rep->cpu_max_percent >= 0.0 ? rep->cpu_max_percent
                                             : rep->cpu.total_usage_percent,
                 rep->cpu_hot_samples, rep->cpu_ok_samples, WT_CPU_BUSY_PCT);
    } else {
        snprintf(r->reason, sizeof(r->reason),
                 "Total CPU usage was %.1f%% during the sample. This is normal "
                 "under active workloads but worth reviewing if unexpected.",
                 rep->cpu.total_usage_percent);
    }
    wt_str_set(r->action, sizeof(r->action),
               "Review top CPU consumers with 'wintune top'.");
    r->severity = WT_SEVERITY_INFO;
    r->risk = WT_RISK_NONE;
    wt_rec_set_confidence(r, rep, WT_CONF_CPU, 75);
}

static void wt_check_boot(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (!rep->boot_ok) {
        return;
    }

    const WT_BootReport *b = &rep->boot;
    const WT_BootHistory *h = &b->history;
    int sustained_slow = 0;
    int conf = 70;

    /* Phase 32: avoid single-boot noise. Prefer a pattern across history. */
    if (h->count >= 2 && h->slow_count >= 2) {
        sustained_slow = 1;
        conf = 88;
    } else if (h->count >= 3 && h->avg_duration_ms >= WT_BOOT_SLOW_MS) {
        sustained_slow = 1;
        conf = 82;
    } else if (h->count >= 2 && b->boot_duration_ms >= WT_BOOT_SLOW_MS &&
               h->avg_duration_ms >= WT_BOOT_SLOW_MS) {
        sustained_slow = 1;
        conf = 78;
    }

    if (sustained_slow) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-BOOT-001");
            wt_str_set(r->title, sizeof(r->title),
                       "Recent boots are slower than expected");
            snprintf(r->reason, sizeof(r->reason),
                     "Across %zu recent boots, average duration is %lu ms "
                     "(%.1f s); %u of %zu were above 60 s. Last boot was "
                     "%s (%.1f s). Sustained slow boots often indicate "
                     "startup apps, drivers, or disk contention — not a "
                     "one-off spike.",
                     h->count, h->avg_duration_ms,
                     h->avg_duration_ms / 1000.0, h->slow_count, h->count,
                     wt_boot_kind_name(b->last_boot_kind),
                     b->boot_duration_ms / 1000.0);
            wt_str_set(r->action, sizeof(r->action),
                       "Run 'wintune boot analyze' and review startup apps.");
            r->severity = (h->avg_duration_ms >= 120000u ||
                           b->boot_duration_ms >= 120000u)
                              ? WT_SEVERITY_MEDIUM
                              : WT_SEVERITY_LOW;
            r->risk = WT_RISK_NONE;
            r->confidence_percent =
                wt_confidence_compute(rep, WT_CONF_BOOT, conf);
            wt_confidence_basis(rep, WT_CONF_BOOT, r->confidence_basis,
                                sizeof(r->confidence_basis));
        }
    }

    if (b->is_degraded) {
        int deg_base = 80;
        int deg_conf;
        if (h->count >= 2 && h->degraded_count >= 2) {
            deg_base = 88;
        } else if (h->count <= 1) {
            deg_base = 60;
        }
        deg_conf = wt_confidence_compute(rep, WT_CONF_BOOT, deg_base);
        if (wt_confidence_should_emit(rep, WT_CONF_BOOT, deg_conf)) {
            WT_Recommendation *r = wt_rec_add(out);
            if (r != NULL) {
                wt_str_set(r->id, sizeof(r->id), "WT-BOOT-002");
                wt_str_set(r->title, sizeof(r->title),
                           "Windows detected boot performance degradation");
                snprintf(r->reason, sizeof(r->reason),
                         "The Diagnostic-Performance log reports boot "
                         "degradation on the last %s boot.%s%s",
                         wt_boot_kind_name(b->last_boot_kind),
                         (h->degraded_count > 1)
                             ? " Multiple recent boots were degraded."
                             : " Confirm with another reboot before major "
                               "changes.",
                         b->degradation_summary[0] != L'\0'
                             ? " See 'wintune boot analyze' for component "
                               "details."
                             : "");
                wt_str_set(r->action, sizeof(r->action), "wintune boot analyze");
                r->severity = WT_SEVERITY_MEDIUM;
                r->risk = WT_RISK_NONE;
                r->confidence_percent = deg_conf;
                wt_confidence_basis(rep, WT_CONF_BOOT, r->confidence_basis,
                                    sizeof(r->confidence_basis));
            }
        }
    }

    for (size_t i = 0; i < b->component_count; ++i) {
        const WT_BootComponent *c = &b->components[i];
        if (c->duration_ms < WT_BOOT_APP_SLOW_MS) {
            continue;
        }
        if (c->kind != WT_BOOT_COMP_APPLICATION &&
            c->kind != WT_BOOT_COMP_DEGRADATION) {
            continue;
        }

        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            break;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-STARTUP-001");
        wt_str_set(r->title, sizeof(r->title),
                   "A startup component had high measured delay");
        snprintf(r->reason, sizeof(r->reason),
                 "Windows measured a startup component adding about %lu ms "
                 "during the last boot/login. %s Review details with "
                 "'wintune boot analyze' and correlate entries with "
                 "'wintune startup --measured'.",
                 c->duration_ms,
                 c->is_disk_heavy
                     ? "Disk I/O during startup was reported."
                     : "");
        wt_str_set(r->action, sizeof(r->action),
                   "wintune startup --measured");
        r->severity = (c->duration_ms >= 10000u) ? WT_SEVERITY_MEDIUM
                                                 : WT_SEVERITY_LOW;
        r->risk = WT_RISK_LOW;
        r->confidence_percent = 75;
        break; /* one startup recommendation per scan to avoid noise */
    }
}

static void wt_check_updates(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (!rep->updates_ok) {
        return;
    }

    const WT_UpdateStatus *u = &rep->updates;

    if (u->reboot_required) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            return;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-UPDATE-001");
        wt_str_set(r->title, sizeof(r->title),
                   "A reboot is pending to finish updates or servicing");
        snprintf(r->reason, sizeof(r->reason),
                 "Windows reports a pending reboot%s%s%s. Leaving the "
                 "machine without rebooting can leave updates incomplete and "
                 "may keep showing restart prompts.",
                 u->reboot_wu ? " (Windows Update)" : "",
                 u->reboot_cbs ? " (component servicing)" : "",
                 u->reboot_pending_file_rename ? " (file operations)" : "");
        wt_str_set(r->action, sizeof(r->action),
                   "Schedule a reboot when convenient; run 'wintune updates'.");
        r->severity = WT_SEVERITY_MEDIUM;
        r->risk = WT_RISK_LOW;
        r->requires_admin = 0;
        r->rollback_available = 0;
        r->confidence_percent = 95;
    }

    if (u->search_available && u->pending_mandatory_count > 0) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            return;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-UPDATE-002");
        wt_str_set(r->title, sizeof(r->title),
                   "Mandatory Windows updates are pending");
        snprintf(r->reason, sizeof(r->reason),
                 "Windows Update reports %lu mandatory update(s) not yet "
                 "installed. WinTune does not install updates; review them in "
                 "Settings > Windows Update.",
                 u->pending_mandatory_count);
        wt_str_set(r->action, sizeof(r->action), "wintune updates");
        r->severity = WT_SEVERITY_MEDIUM;
        r->risk = WT_RISK_NONE;
        r->requires_admin = 0;
        r->rollback_available = 0;
        r->confidence_percent = 90;
    } else if (u->search_available && u->pending_count >= 5) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            return;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-UPDATE-003");
        wt_str_set(r->title, sizeof(r->title),
                   "Many Windows updates are pending");
        snprintf(r->reason, sizeof(r->reason),
                 "Windows Update reports %lu pending update(s). Installing "
                 "them during maintenance can improve security and stability.",
                 u->pending_count);
        wt_str_set(r->action, sizeof(r->action), "wintune updates");
        r->severity = WT_SEVERITY_LOW;
        r->risk = WT_RISK_NONE;
        r->confidence_percent = 85;
    }

    if (u->wu_service_running == 0) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            return;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-UPDATE-004");
        wt_str_set(r->title, sizeof(r->title),
                   "Windows Update service is not running");
        wt_str_set(r->reason, sizeof(r->reason),
                   "The Windows Update service (wuauserv) is stopped. Updates "
                   "cannot be checked or installed until it is running.");
        wt_str_set(r->action, sizeof(r->action),
                   "Start the Windows Update service or use Settings > Windows Update.");
        r->severity = WT_SEVERITY_LOW;
        r->risk = WT_RISK_NONE;
        r->confidence_percent = 90;
    }
}

static void wt_check_blockers(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (!rep->updates_ok || !rep->updates.reboot_required) {
        return;
    }

    WT_BlockerReport blockers;
    wt_blocker_report_init(&blockers);
    if (wt_collect_blockers(&blockers) != WT_OK) {
        return;
    }
    if (blockers.process_blocker_count == 0 &&
        blockers.locked_file_count == 0) {
        return;
    }

    WT_Recommendation *r = wt_rec_add(out);
    if (r == NULL) {
        return;
    }
    wt_str_set(r->id, sizeof(r->id), "WT-BLOCKER-001");
    wt_str_set(r->title, sizeof(r->title),
               "Applications or file locks may block restart");
    snprintf(r->reason, sizeof(r->reason),
             "WinTune detected %lu application(s) with shutdown blocks and "
             "%lu locked file path(s). These can prevent Windows Update or "
             "servicing from completing until they are closed.",
             blockers.process_blocker_count, blockers.locked_file_count);
    wt_str_set(r->action, sizeof(r->action), "wintune blockers");
    r->severity = WT_SEVERITY_MEDIUM;
    r->risk = WT_RISK_NONE;
    r->requires_admin = 0;
    r->rollback_available = 0;
    r->confidence_percent = 85;
}

static int wt_startup_is_systemish(const WT_StartupEntry *e)
{
    if (e == NULL) {
        return 1;
    }
    if (e->identity.origin == WT_ORIGIN_MICROSOFT ||
        e->identity.signature == WT_SIG_SIGNED_MICROSOFT) {
        return 1;
    }
    const wchar_t *cmd = e->command;
    if (cmd[0] == L'\0') {
        return 0;
    }
    if (wcsstr(cmd, L"\\Windows\\") != NULL ||
        wcsstr(cmd, L"\\Microsoft\\") != NULL ||
        wcsstr(cmd, L"Windows Defender") != NULL ||
        wcsstr(cmd, L"SecurityHealth") != NULL) {
        return 1;
    }
    return 0;
}

static void wt_check_startup_actions(const WT_ScanReport *report,
                                     WT_RecommendationList *out)
{
    WT_StartupEntry entries[WT_MAX_STARTUP_ENTRIES];
    size_t count = 0;
    if (wt_collect_startup_entries(entries, ARRAYSIZE(entries), &count) != WT_OK) {
        return;
    }
    if (report != NULL && report->boot_ok) {
        wt_startup_apply_measured(entries, count, &report->boot);
    }

    for (size_t i = 0; i < count; ++i) {
        WT_StartupEntry *e = &entries[i];
        WT_ImpactInput in;
        WT_ImpactScore score;
        wt_impact_input_from_startup(e, &in);
        if (report != NULL && report->processes_ok) {
            wt_impact_attach_runtime(&in, report->top_processes,
                                     report->top_process_count);
        }
        wt_impact_score_compute(&in, &score);
        wt_impact_apply_to_startup(e, &score);
    }

    for (size_t i = 0; i < count; ++i) {
        const WT_StartupEntry *e = &entries[i];
        if (!e->enabled) {
            continue;
        }
        if (e->impact_score < WT_IMPACT_RECOMMEND_SCORE_MIN ||
            e->impact_confidence < WT_IMPACT_RECOMMEND_CONF_MIN) {
            continue;
        }
        if (wt_startup_is_systemish(e)) {
            continue;
        }

        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            break;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-STARTUP-002");
        wt_str_set(r->title, sizeof(r->title),
                   "Review disabling a high-impact startup entry");
        if (e->measured_available) {
            snprintf(r->reason, sizeof(r->reason),
                     "Startup entry '%ls' (%s) scored %d/100 impact "
                     "(confidence %d%%) with about %lu ms measured login delay. "
                     "Disabling it can reduce login time; re-enable via Task "
                     "Manager or WinTune rollback.",
                     e->name,
                     wt_publisher_origin_name(e->identity.origin),
                     e->impact_score, e->impact_confidence, e->measured_ms);
        } else {
            snprintf(r->reason, sizeof(r->reason),
                     "Startup entry '%ls' (%s) scored %d/100 impact "
                     "(confidence %d%%) from publisher/heuristic/runtime "
                     "evidence. Disabling it can reduce login overhead; "
                     "re-enable via Task Manager or WinTune rollback.",
                     e->name,
                     wt_publisher_origin_name(e->identity.origin),
                     e->impact_score, e->impact_confidence);
        }
        snprintf(r->action, sizeof(r->action),
                 "wintune apply WT-STARTUP-DISABLE \"%ls\"", e->id);
        r->severity = WT_SEVERITY_MEDIUM;
        r->risk = WT_RISK_LOW;
        r->requires_admin = (wcsstr(e->id, L"HKLM") != NULL) ? 1 : 0;
        r->rollback_available = 1;
        r->confidence_percent = e->impact_confidence;
        break;
    }
}

static void wt_check_task_actions(const WT_ScanReport *report,
                                  WT_RecommendationList *out)
{
    WT_ScheduledTask tasks[WT_MAX_SCHEDULED_TASKS];
    size_t count = 0;
    if (wt_collect_scheduled_tasks(tasks, ARRAYSIZE(tasks), &count,
                                   WT_TASK_FILTER_LOGON) != WT_OK) {
        return;
    }
    if (report != NULL && report->boot_ok) {
        wt_tasks_apply_measured(tasks, count, &report->boot);
    }

    for (size_t i = 0; i < count; ++i) {
        WT_ScheduledTask *t = &tasks[i];
        WT_ImpactInput in;
        WT_ImpactScore score;
        wt_impact_input_from_task(t, &in);
        if (report != NULL && report->processes_ok) {
            wt_impact_attach_runtime(&in, report->top_processes,
                                     report->top_process_count);
        }
        wt_impact_score_compute(&in, &score);
        wt_impact_apply_to_task(t, &score);
    }

    for (size_t i = 0; i < count; ++i) {
        const WT_ScheduledTask *t = &tasks[i];
        if (!t->enabled) {
            continue;
        }
        if (wt_task_is_protected(t)) {
            continue;
        }
        if (t->impact_score < WT_IMPACT_RECOMMEND_SCORE_MIN ||
            t->impact_confidence < WT_IMPACT_RECOMMEND_CONF_MIN) {
            continue;
        }
        if (t->delay_seconds > 0) {
            continue;
        }

        WT_Recommendation *r = wt_rec_add(out);
        if (r == NULL) {
            break;
        }
        wt_str_set(r->id, sizeof(r->id), "WT-TASK-001");
        wt_str_set(r->title, sizeof(r->title),
                   "Consider delaying a logon scheduled task");
        if (t->measured_available) {
            snprintf(r->reason, sizeof(r->reason),
                     "Logon task '%ls' scored %d/100 impact (confidence %d%%) "
                     "with about %lu ms measured login delay. Delaying it by "
                     "30 seconds can spread login work without removing the task.",
                     t->name, t->impact_score, t->impact_confidence,
                     t->measured_ms);
        } else {
            snprintf(r->reason, sizeof(r->reason),
                     "Logon task '%ls' scored %d/100 impact (confidence %d%%). "
                     "Delaying it by 30 seconds can spread login work without "
                     "removing the task.",
                     t->name, t->impact_score, t->impact_confidence);
        }
        snprintf(r->action, sizeof(r->action),
                 "wintune apply WT-TASK-DELAY \"%ls\" --seconds 30", t->id);
        r->severity = WT_SEVERITY_LOW;
        r->risk = WT_RISK_LOW;
        r->requires_admin = 0;
        r->rollback_available = 1;
        r->confidence_percent = t->impact_confidence;
        break;
    }
}

static void wt_check_uninstall(const WT_ScanReport *report,
                               WT_RecommendationList *out)
{
    (void)report;
    WT_UninstallAdvice *advice =
        (WT_UninstallAdvice *)malloc(sizeof(WT_UninstallAdvice));
    if (advice == NULL) {
        return;
    }
    if (wt_collect_uninstall_advice(advice, 1, 0) != WT_OK) {
        free(advice);
        return;
    }
    if (advice->candidate_count == 0) {
        free(advice);
        return;
    }

    WT_Recommendation *r = wt_rec_add(out);
    if (r == NULL) {
        free(advice);
        return;
    }
    wt_str_set(r->id, sizeof(r->id), "WT-UNINSTALL-001");
    wt_str_set(r->title, sizeof(r->title),
               "Review installed apps that may be safe to remove");
    snprintf(r->reason, sizeof(r->reason),
             "%zu installed-app candidate(s) match high-impact startup entries. "
             "WinTune never uninstalls software.",
             advice->candidate_count);
    wt_str_set(r->action, sizeof(r->action),
               "wintune apps   (then Settings / winget - you uninstall)");
    r->severity = WT_SEVERITY_LOW;
    r->risk = WT_RISK_NONE;
    r->requires_admin = 0;
    r->rollback_available = 0;
    r->confidence_percent = 70;
    wt_str_set(r->confidence_basis, sizeof(r->confidence_basis),
               "ARP metadata + startup correlation");
    free(advice);
}

static void wt_check_gpu(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    int conf;
    if (!rep->gpu_ok || !rep->gpu.utilization_ok ||
        rep->gpu.max_utilization_percent < 90.0) {
        return;
    }
    conf = wt_confidence_compute(rep, WT_CONF_GPU, 70);
    if (!wt_confidence_should_emit(rep, WT_CONF_GPU, conf)) {
        return;
    }
    WT_Recommendation *r = wt_rec_add(out);
    if (r == NULL) {
        return;
    }
    const WT_GpuAdapter *hot = NULL;
    for (size_t i = 0; i < rep->gpu.adapter_count; ++i) {
        const WT_GpuAdapter *a = &rep->gpu.adapters[i];
        if (!a->utilization_ok) {
            continue;
        }
        if (hot == NULL ||
            a->utilization_percent > hot->utilization_percent) {
            hot = a;
        }
    }
    wt_str_set(r->id, sizeof(r->id), "WT-GPU-001");
    wt_str_set(r->title, sizeof(r->title), "GPU engines are very busy");
    if (hot != NULL) {
        char name_utf8[160];
        WideCharToMultiByte(CP_UTF8, 0, hot->name, -1, name_utf8,
                            sizeof(name_utf8), NULL, NULL);
        snprintf(r->reason, sizeof(r->reason),
                 "GPU engine utilization peaked around %.0f%% on \"%s\" during "
                 "the sample. High GPU load can make the desktop feel laggy "
                 "(compositing, browsers, games, video encode). This is "
                 "informational — WinTune does not change GPU drivers or clocks.",
                 hot->utilization_percent, name_utf8);
    } else {
        snprintf(r->reason, sizeof(r->reason),
                 "GPU engine utilization peaked around %.0f%% during the sample. "
                 "High GPU load can make the desktop feel laggy.",
                 rep->gpu.max_utilization_percent);
    }
    wt_str_set(r->action, sizeof(r->action),
               "Review GPU-heavy apps; wait for encodes/games to finish.");
    r->severity = WT_SEVERITY_INFO;
    r->risk = WT_RISK_NONE;
    wt_rec_set_confidence(r, rep, WT_CONF_GPU, 70);
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
    wt_check_memory_commit(report, out);
    wt_check_memory_hard_faults(report, out);
    wt_check_disk(report, out);
    wt_check_storage_health(out);
    wt_check_reliability(out);
    wt_check_maintenance(report, out);
    wt_check_cpu(report, out);
    wt_check_gpu(report, out);
    wt_check_boot(report, out);
    wt_check_updates(report, out);
    wt_check_blockers(report, out);
    wt_check_startup_actions(report, out);
    wt_check_task_actions(report, out);
    wt_check_uninstall(report, out);

    return WT_OK;
}
