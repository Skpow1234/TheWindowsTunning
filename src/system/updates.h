#ifndef WINTUNE_UPDATES_H
#define WINTUNE_UPDATES_H

#include <stddef.h>
#include <windows.h>

#include "common/error.h"

typedef struct WT_PendingUpdate {
    wchar_t title[256];
    wchar_t update_id[128];
    int mandatory;
    int downloaded;
    int reboot_required;
} WT_PendingUpdate;

#define WT_MAX_PENDING_UPDATES 32

typedef struct WT_UpdateStatus {
    int reboot_required;
    int reboot_wu;
    int reboot_cbs;
    int reboot_pending_file_rename;

    int wu_service_running;

    int last_check_available;
    FILETIME last_check_utc;

    int last_install_available;
    FILETIME last_install_utc;
    wchar_t last_install_title[256];

    int search_available;
    unsigned long pending_count;
    unsigned long pending_mandatory_count;
    unsigned long pending_reboot_count;
    WT_PendingUpdate pending[WT_MAX_PENDING_UPDATES];

    wchar_t note[256];
} WT_UpdateStatus;

void wt_update_status_init(WT_UpdateStatus *status);

/* Fast read-only collection: reboot flags, WU service state, last check/install
 * timestamps from registry. Does not call Windows Update search. */
WT_Result wt_collect_update_status_fast(WT_UpdateStatus *status);

/* Full status including a Windows Update Agent search for pending updates.
 * Can take several seconds; never installs anything. */
WT_Result wt_collect_update_status(WT_UpdateStatus *status);

/* Adds pending update list to an existing fast status (used by recommend). */
WT_Result wt_search_pending_updates(WT_UpdateStatus *status);

WT_Result wt_format_filetime_iso8601_utc(const FILETIME *ft, char *out, size_t out_cap);

#endif /* WINTUNE_UPDATES_H */
