#ifndef WINTUNE_PROCESS_H
#define WINTUNE_PROCESS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

#define WT_PROCESS_NAME_MAX 260

typedef enum WT_ProcessSort {
    WT_PROCESS_SORT_MEMORY = 0,
    WT_PROCESS_SORT_CPU,
    WT_PROCESS_SORT_DISK
} WT_ProcessSort;

typedef struct WT_ProcessInfo {
    unsigned long pid;
    wchar_t name[WT_PROCESS_NAME_MAX];
    unsigned long long working_set_bytes;
    unsigned long long private_bytes;
    unsigned long long read_bytes;       /* cumulative I/O since process start */
    unsigned long long write_bytes;
    double cpu_percent;                  /* -1.0 when not measured */
    double disk_read_bytes_per_sec;      /* -1.0 when not measured */
    double disk_write_bytes_per_sec;     /* -1.0 when not measured */
} WT_ProcessInfo;

/* Enumerates running processes (Tool Help) and collects per-process memory and
 * I/O counters (PSAPI). Processes that cannot be opened are still listed with
 * zeroed counters. CPU and disk rates are not measured (see snapshot API). */
WT_Result wt_collect_processes(WT_ProcessInfo *out,
                               size_t capacity,
                               size_t *out_count);

void wt_sort_processes_by_memory(WT_ProcessInfo *items, size_t count);
void wt_sort_processes_by_cpu(WT_ProcessInfo *items, size_t count);
void wt_sort_processes_by_disk(WT_ProcessInfo *items, size_t count);
void wt_sort_processes(WT_ProcessInfo *items, size_t count, WT_ProcessSort sort);

/* Returns the top `limit` processes after a short sample window. When
 * `sample_ms` > 0, enriches entries with PDH CPU and disk I/O rates. */
WT_Result wt_collect_top_processes(WT_ProcessInfo *out,
                                   size_t limit,
                                   WT_ProcessSort sort,
                                   unsigned int sample_ms,
                                   size_t *out_count);

/* Backward-compatible wrapper: top processes by memory without CPU/disk rates. */
WT_Result wt_collect_top_processes_by_memory(WT_ProcessInfo *out,
                                             size_t limit,
                                             size_t *out_count);

/* Applies PDH CPU and IO-counter deltas to an existing process list. */
WT_Result wt_enrich_process_metrics(WT_ProcessInfo *items,
                                    size_t count,
                                    unsigned int sample_ms);

#endif /* WINTUNE_PROCESS_H */
