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

typedef struct WT_BootComponent {
    wchar_t name[128];
    wchar_t detail[256];
    WT_BootComponentKind kind;
    unsigned long duration_ms;
    int is_disk_heavy;
} WT_BootComponent;

#define WT_MAX_BOOT_COMPONENTS 64

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

    WT_BootComponent components[WT_MAX_BOOT_COMPONENTS];
    size_t component_count;

    wchar_t source[32];       /* "event_log", "etl", or "event_log+etl" */
    wchar_t trace_path[MAX_PATH];
    unsigned long etl_event_count;
} WT_BootReport;

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

/* Primary analyze entry: event log plus optional .etl supplement. */
WT_Result wt_collect_boot_report(WT_BootReport *report,
                                 const wchar_t *etl_path_opt);

const char *wt_boot_component_kind_name(WT_BootComponentKind kind);

/* Match startup entry names/commands against measured boot components. */
void wt_boot_apply_measured_startup(const WT_BootReport *boot,
                                    wchar_t *entry_name, wchar_t *entry_command,
                                    unsigned long *out_ms, int *out_matched);

#endif /* WINTUNE_BOOT_H */
