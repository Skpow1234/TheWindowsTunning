#ifndef WINTUNE_TRAY_STATUS_H
#define WINTUNE_TRAY_STATUS_H

#include <windows.h>

/* Read-only summary: prefers a quick live sample, falls back to last_scan.json. */
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
} WT_TrayStatusSummary;

void wt_tray_status_refresh(WT_TrayStatusSummary *summary);

/* Hover tip text for Shell_NotifyIcon (must fit NOTIFYICONDATA.szTip). */
void wt_tray_status_format_tip(const WT_TrayStatusSummary *s, wchar_t *out,
                               size_t out_count);

/* Modeless status window (read-only). `owner` may be NULL. */
HWND wt_tray_status_show(HWND owner);

#endif /* WINTUNE_TRAY_STATUS_H */
