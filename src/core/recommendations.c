#include "core/recommendations.h"
#include "system/power.h"
#include "system/boot.h"
#include "system/updates.h"
#include "system/blockers.h"

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

static void wt_check_boot(const WT_ScanReport *rep, WT_RecommendationList *out)
{
    if (!rep->boot_ok) {
        return;
    }

    const WT_BootReport *b = &rep->boot;

    if (b->boot_duration_ms >= WT_BOOT_SLOW_MS) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-BOOT-001");
            wt_str_set(r->title, sizeof(r->title),
                       "Last boot took longer than expected");
            snprintf(r->reason, sizeof(r->reason),
                     "Windows reported a boot duration of %lu ms (%.1f s). "
                     "Boot times above 60 s often indicate slow drivers, "
                     "startup apps, or disk contention during login.",
                     b->boot_duration_ms, b->boot_duration_ms / 1000.0);
            wt_str_set(r->action, sizeof(r->action),
                       "Run 'wintune boot analyze' and review startup apps.");
            r->severity = (b->boot_duration_ms >= 120000u)
                              ? WT_SEVERITY_MEDIUM : WT_SEVERITY_LOW;
            r->risk = WT_RISK_NONE;
            r->confidence_percent = 85;
        }
    }

    if (b->is_degraded) {
        WT_Recommendation *r = wt_rec_add(out);
        if (r != NULL) {
            wt_str_set(r->id, sizeof(r->id), "WT-BOOT-002");
            wt_str_set(r->title, sizeof(r->title),
                       "Windows detected boot performance degradation");
            snprintf(r->reason, sizeof(r->reason),
                     "The Diagnostic-Performance log reports boot degradation. "
                     "%s",
                     b->degradation_summary[0] != L'\0'
                         ? "See 'wintune boot analyze' for component details."
                         : "Review slow components with 'wintune boot analyze'.");
            wt_str_set(r->action, sizeof(r->action), "wintune boot analyze");
            r->severity = WT_SEVERITY_MEDIUM;
            r->risk = WT_RISK_NONE;
            r->confidence_percent = 80;
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
    wt_check_boot(report, out);
    wt_check_updates(report, out);
    wt_check_blockers(report, out);

    return WT_OK;
}
