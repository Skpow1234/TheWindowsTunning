#ifndef WINTUNE_TRAY_STATUS_H
#define WINTUNE_TRAY_STATUS_H

#include <windows.h>

/* Read-only summary parsed from last_scan.json or a cached service scan. */
typedef struct WT_TrayStatusSummary {
    int available;
    int service_connected;
    wchar_t hostname[64];
    wchar_t os_line[128];
    double cpu_percent;
    double memory_percent;
    wchar_t power_plan[64];
    wchar_t source_path[MAX_PATH];
    wchar_t source_label[64];
} WT_TrayStatusSummary;

void wt_tray_status_refresh(WT_TrayStatusSummary *summary);

/* Modeless status window (read-only). `owner` may be NULL. */
HWND wt_tray_status_show(HWND owner);

#endif /* WINTUNE_TRAY_STATUS_H */
