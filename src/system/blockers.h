#ifndef WINTUNE_BLOCKERS_H
#define WINTUNE_BLOCKERS_H

#include <windows.h>

#include "common/error.h"

#define WT_MAX_BLOCKER_PROCESSES 64
#define WT_MAX_LOCKED_FILES      32
#define WT_MAX_LOCKED_PROCESSES  8

typedef enum WT_BlockerKind {
    WT_BLOCKER_SHUTDOWN = 0,
    WT_BLOCKER_FILE_LOCK
} WT_BlockerKind;

typedef struct WT_BlockerProcess {
    unsigned long pid;
    wchar_t name[260];
    wchar_t app_name[256];
    WT_BlockerKind kind;
    wchar_t reason[512];
} WT_BlockerProcess;

typedef struct WT_LockedFile {
    wchar_t path[512];
    unsigned long process_count;
    unsigned long pids[WT_MAX_LOCKED_PROCESSES];
    wchar_t process_names[WT_MAX_LOCKED_PROCESSES][260];
} WT_LockedFile;

typedef struct WT_BlockerReport {
    int reboot_pending;
    int reboot_wu;
    int reboot_cbs;
    int reboot_pending_file_rename;

    unsigned long process_blocker_count;
    WT_BlockerProcess process_blockers[WT_MAX_BLOCKER_PROCESSES];

    unsigned long locked_file_count;
    WT_LockedFile locked_files[WT_MAX_LOCKED_FILES];

    unsigned long pending_rename_file_count;

    wchar_t note[256];
} WT_BlockerReport;

void wt_blocker_report_init(WT_BlockerReport *report);

/* Detect shutdown-blocking applications and file locks (Restart Manager).
 * Read-only; never closes processes. */
WT_Result wt_collect_blockers(WT_BlockerReport *report);

#endif /* WINTUNE_BLOCKERS_H */
