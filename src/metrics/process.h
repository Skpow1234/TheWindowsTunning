#ifndef WINTUNE_PROCESS_H
#define WINTUNE_PROCESS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

#define WT_PROCESS_NAME_MAX 260

typedef struct WT_ProcessInfo {
    unsigned long pid;
    wchar_t name[WT_PROCESS_NAME_MAX];
    unsigned long long working_set_bytes;
    unsigned long long private_bytes;
    unsigned long long read_bytes;       /* cumulative I/O since process start */
    unsigned long long write_bytes;
    double cpu_percent;                  /* -1.0 when not measured (Phase 1) */
} WT_ProcessInfo;

/* Enumerates running processes (Tool Help) and collects per-process memory and
 * I/O counters (PSAPI). Processes that cannot be opened (e.g. protected system
 * processes) are still listed with zeroed counters. */
WT_Result wt_collect_processes(WT_ProcessInfo *out,
                               size_t capacity,
                               size_t *out_count);

/* Sorts an array of processes by working set, descending. */
void wt_sort_processes_by_memory(WT_ProcessInfo *items, size_t count);

#endif /* WINTUNE_PROCESS_H */
