#ifndef WINTUNE_UNINSTALL_H
#define WINTUNE_UNINSTALL_H

#include <stddef.h>
#include <wchar.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "common/error.h"

#define WT_MAX_INSTALLED_APPS 512

typedef struct WT_InstalledApp {
    wchar_t key_id[128];          /* Uninstall subkey name */
    wchar_t display_name[256];
    wchar_t publisher[128];
    wchar_t version[64];
    wchar_t install_location[MAX_PATH];
    wchar_t uninstall_string[512]; /* display only - never executed */
    unsigned long estimated_kb;   /* 0 if unknown */
    int system_component;         /* SystemComponent=1 */
    int is_microsoft;
    int startup_related;          /* matched a high-impact startup entry */
    int candidate;                /* worth calm uninstall review */
    char reason[192];             /* why flagged (UTF-8) */
} WT_InstalledApp;

typedef struct WT_UninstallAdvice {
    WT_InstalledApp apps[WT_MAX_INSTALLED_APPS];
    size_t count;
    size_t candidate_count;
    size_t scanned_keys;
} WT_UninstallAdvice;

/* Read-only ARP / Uninstall registry scan. Never runs UninstallString.
 * When correlate_startup is non-zero, marks apps that match high-impact
 * startup entries as candidates. */
WT_Result wt_collect_uninstall_advice(WT_UninstallAdvice *out,
                                      int correlate_startup);

#endif /* WINTUNE_UNINSTALL_H */
