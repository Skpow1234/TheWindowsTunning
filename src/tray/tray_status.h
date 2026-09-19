#ifndef WINTUNE_TRAY_STATUS_H
#define WINTUNE_TRAY_STATUS_H

#include <windows.h>

#include "common/error.h"

#define WT_TRAY_STATUS_MSG_QUICK_SCAN (WM_APP + 40)
#define WT_TRAY_QUICK_REC_LINES 4

/* Read-only summary: prefers a quick live sample, falls back to last_scan.json.
 * Optional mini-doctor fields are filled by Quick scan (Phase 42). */
typedef struct WT_TrayStatusSummary {
    int available;
    int service_connected;
    int live_sample;
    wchar_t hostname[64];
    wchar_t os_line[128];
    double cpu_percent;
    double memory_percent;
    wchar_t power_plan[64];
    wchar_t power_source[32];
    wchar_t source_path[MAX_PATH];
    wchar_t source_label[64];

    /* Phase 42 — last Quick scan (in-process, read-only) */
    int quick_scan_ok;
    wchar_t quick_scan_time[32];
    double disk_active_percent; /* -1 if unknown */
    size_t rec_count;
    size_t applyable_count;
    wchar_t rec_lines[WT_TRAY_QUICK_REC_LINES][160];
    size_t rec_line_count;
    wchar_t next_hint[192];
} WT_TrayStatusSummary;

void wt_tray_status_refresh(WT_TrayStatusSummary *summary);

/* Short local scan + recommendations into summary. Never applies changes. */
WT_Result wt_tray_status_quick_scan(WT_TrayStatusSummary *summary);

/* Hover tip text for Shell_NotifyIcon (must fit NOTIFYICONDATA.szTip). */
void wt_tray_status_format_tip(const WT_TrayStatusSummary *s, wchar_t *out,
                               size_t out_count);

/* Modeless status window (read-only). `owner` may be NULL. */
HWND wt_tray_status_show(HWND owner);

/* Ask an open status window to run Quick scan (no-op if hwnd invalid). */
void wt_tray_status_post_quick_scan(HWND hwnd);

#endif /* WINTUNE_TRAY_STATUS_H */
