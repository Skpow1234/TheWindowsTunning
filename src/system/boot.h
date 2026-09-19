#ifndef WINTUNE_BOOT_H
#define WINTUNE_BOOT_H

#include <stddef.h>
#include <windows.h>

#include "common/error.h"

typedef enum WT_BootComponentKind {
    WT_BOOT_COMP_UNKNOWN = 0,
    WT_BOOT_COMP_SERVICE,
    WT_BOOT_COMP_DRIVER,
    WT_BOOT_COMP_APPLICATION,
    WT_BOOT_COMP_DEGRADATION
} WT_BootComponentKind;

/* Phase 32: cold vs warm/hybrid (Fast Startup) classification. */
typedef enum WT_BootKind {
    WT_BOOT_KIND_UNKNOWN = 0,
    WT_BOOT_KIND_COLD,
    WT_BOOT_KIND_WARM
} WT_BootKind;

typedef struct WT_BootComponent {
    wchar_t name[128];
    wchar_t detail[256];
    WT_BootComponentKind kind;
    unsigned long duration_ms;
    int is_disk_heavy;
} WT_BootComponent;

#define WT_MAX_BOOT_COMPONENTS 64
#define WT_MAX_BOOT_HISTORY    8

typedef struct WT_BootHistoryEntry {
    unsigned long boot_duration_ms;
    unsigned long main_path_ms;
    unsigned long post_boot_ms;
    unsigned long kernel_init_ms;
    unsigned long driver_init_ms;
    WT_BootKind kind;
    int is_degraded;
    int is_reboot_after_install;
    char boot_start_utc[40]; /* BootStartTime when present */
} WT_BootHistoryEntry;

typedef struct WT_BootHistory {
    WT_BootHistoryEntry entries[WT_MAX_BOOT_HISTORY];
    size_t count;
    unsigned long avg_duration_ms;
    unsigned long avg_cold_ms;
    unsigned long avg_warm_ms;
    unsigned int cold_count;
    unsigned int warm_count;
    unsigned int unknown_count;
    unsigned int slow_count; /* duration >= 60 s */
    unsigned int degraded_count;
} WT_BootHistory;

/* Summarized boot/login performance from Windows Diagnostic-Performance
 * events (ETW-backed event log). Not a raw ETW dump. */
typedef struct WT_BootReport {
    unsigned long boot_duration_ms;
    unsigned long main_path_ms;
    unsigned long kernel_init_ms;
    unsigned long driver_init_ms;
    unsigned long post_boot_ms;
    int is_degraded;
    wchar_t degradation_summary[256];

    WT_BootKind last_boot_kind; /* Phase 32 */
    WT_BootHistory history;     /* recent Event 100 samples */

    WT_BootComponent components[WT_MAX_BOOT_COMPONENTS];
    size_t component_count;

    wchar_t source[32];       /* "event_log", "etl", or "event_log+etl" */
    wchar_t trace_path[MAX_PATH];
    unsigned long etl_event_count;
} WT_BootReport;

/* Phase 31: Autologger armed for the next reboot. */
typedef enum WT_BootArmState {
    WT_BOOT_ARM_IDLE = 0,
    WT_BOOT_ARM_PENDING_REBOOT, /* Autologger configured; reboot not yet done */
    WT_BOOT_ARM_CAPTURING,      /* Reboot done; session still writing */
    WT_BOOT_ARM_READY           /* Reboot ETL present; safe to analyze */
} WT_BootArmState;

typedef struct WT_BootArmStatus {
    WT_BootArmState state;
    wchar_t session_name[64];
    wchar_t etl_path[MAX_PATH];
    char armed_utc[40];
    int reboot_occurred;
    int etl_exists;
    unsigned long long etl_bytes;
} WT_BootArmStatus;

void wt_boot_report_init(WT_BootReport *report);

/* Reads the latest boot summary and degradation events from
 * Microsoft-Windows-Diagnostics-Performance/Operational. */
WT_Result wt_collect_boot_from_event_log(WT_BootReport *report);

/* Runs a live ETW login trace for `duration_ms` and writes an .etl file.
 * Admin may be required. */
WT_Result wt_boot_trace_login(unsigned duration_ms, wchar_t *etl_path,
                              size_t etl_path_count);

/* Counts events in an .etl file (optional supplement to event-log analysis). */
WT_Result wt_boot_analyze_etl(const wchar_t *etl_path, WT_BootReport *report);

/* Primary analyze entry: event log plus optional .etl supplement.
 * When etl_path_opt is NULL, auto-picks a Phase 31 reboot ETL if ready. */
WT_Result wt_collect_boot_report(WT_BootReport *report,
                                 const wchar_t *etl_path_opt);

/* Phase 31 — reboot-spanning Autologger (requires admin). */
WT_Result wt_boot_arm_next(void);
WT_Result wt_boot_disarm(int keep_etl);
WT_Result wt_boot_arm_status(WT_BootArmStatus *out);
WT_Result wt_boot_stop_armed_session(void);
/* Fills out with reboot ETL path when READY/CAPTURING and file exists. */
WT_Result wt_boot_resolve_reboot_etl(wchar_t *out, size_t count);

const char *wt_boot_component_kind_name(WT_BootComponentKind kind);
const char *wt_boot_kind_name(WT_BootKind kind);
const char *wt_boot_arm_state_name(WT_BootArmState state);

/* Match startup entry names/commands against measured boot components. */
void wt_boot_apply_measured_startup(const WT_BootReport *boot,
                                    wchar_t *entry_name, wchar_t *entry_command,
                                    unsigned long *out_ms, int *out_matched);

#endif /* WINTUNE_BOOT_H */
