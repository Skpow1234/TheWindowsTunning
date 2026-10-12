#ifndef WINTUNE_PROCESS_H
#define WINTUNE_PROCESS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "system/file_identity.h"

#define WT_PROCESS_NAME_MAX 260

typedef enum WT_ProcessSort {
    WT_PROCESS_SORT_MEMORY = 0,
    WT_PROCESS_SORT_CPU,
    WT_PROCESS_SORT_DISK,
    WT_PROCESS_SORT_NETWORK
} WT_ProcessSort;

typedef enum WT_IntegrityLevel {
    WT_INTEGRITY_UNKNOWN = 0,
    WT_INTEGRITY_UNTRUSTED,
    WT_INTEGRITY_LOW,
    WT_INTEGRITY_MEDIUM,
    WT_INTEGRITY_MEDIUM_PLUS,
    WT_INTEGRITY_HIGH,
    WT_INTEGRITY_SYSTEM,
    WT_INTEGRITY_PROTECTED
} WT_IntegrityLevel;

typedef struct WT_ProcessInfo {
    unsigned long pid;
    unsigned long parent_pid;           /* ToolHelp parent; may be reused */
    unsigned long session_id;
    int session_ok;                      /* 0 when the session could not be read */
    WT_IntegrityLevel integrity;         /* UNKNOWN when the token cannot be read */
    int wait_checked;                    /* 1 when a wait-chain query succeeded */
    int hung;                            /* 1 when the wait chain is a hang or deadlock */
    int wait_cycle;                      /* 1 when the wait chain contains a cycle */
    wchar_t name[WT_PROCESS_NAME_MAX];
    unsigned long long working_set_bytes;
    unsigned long long private_bytes;
    unsigned long long read_bytes;       /* cumulative I/O since process start */
    unsigned long long write_bytes;
    double cpu_percent;                  /* -1.0 when not measured */
    double disk_read_bytes_per_sec;      /* -1.0 when not measured */
    double disk_write_bytes_per_sec;     /* -1.0 when not measured */
    double net_recv_bytes_per_sec;       /* -1.0 when not measured (opt-in) */
    double net_send_bytes_per_sec;       /* -1.0 when not measured (opt-in) */
    WT_FileIdentity identity;            /* publisher / Authenticode (top only) */
} WT_ProcessInfo;

/* One page of a full process list. `page` is 1-based. */
#define WT_PROCESS_PAGE_MAX 32
#define WT_PROCESS_ENUM_MAX 8192

typedef struct WT_ProcessPageInfo {
    size_t page;
    size_t page_size;
    size_t page_count;
    size_t total;
    int truncated; /* 1 when more processes exist than WT_PROCESS_ENUM_MAX */
} WT_ProcessPageInfo;

/* Computes the slice for `page` (1-based). `page_size` is clamped to
 * WT_PROCESS_PAGE_MAX. Returns WT_ERR_NOT_FOUND when `page` is past the end. */
WT_Result wt_process_page_bounds(size_t total,
                                 size_t page,
                                 size_t page_size,
                                 size_t *out_offset,
                                 size_t *out_count,
                                 size_t *out_page_count);

const char *wt_integrity_level_name(WT_IntegrityLevel level);

/* Best-effort publisher/signature fill for an existing process list. */
void wt_enrich_process_identity(WT_ProcessInfo *items, size_t count);

/* Read-only wait-chain check for an existing process list (not the full
 * machine). Sets hung/wait_cycle only when a query succeeds. */
void wt_enrich_process_wait_chain(WT_ProcessInfo *items, size_t count);

/* Enumerates running processes (Tool Help) and collects per-process memory and
 * I/O counters (PSAPI). Processes that cannot be opened are still listed with
 * zeroed counters. CPU and disk rates are not measured (see snapshot API). */
WT_Result wt_collect_processes(WT_ProcessInfo *out,
                               size_t capacity,
                               size_t *out_count);

void wt_sort_processes_by_memory(WT_ProcessInfo *items, size_t count);
void wt_sort_processes_by_cpu(WT_ProcessInfo *items, size_t count);
void wt_sort_processes_by_disk(WT_ProcessInfo *items, size_t count);
void wt_sort_processes_by_network(WT_ProcessInfo *items, size_t count);
void wt_sort_processes(WT_ProcessInfo *items, size_t count, WT_ProcessSort sort);

/* Returns the top `limit` processes after a short sample window. When
 * `sample_ms` > 0, enriches entries with PDH CPU and disk I/O rates.
 * When `include_network` is non-zero, also samples TCP Extended Stats
 * (opt-in; higher overhead; no payload capture). */
WT_Result wt_collect_top_processes(WT_ProcessInfo *out,
                                   size_t limit,
                                   WT_ProcessSort sort,
                                   unsigned int sample_ms,
                                   int include_network,
                                   size_t *out_count);

/* Backward-compatible wrapper: top processes by memory without CPU/disk rates. */
WT_Result wt_collect_top_processes_by_memory(WT_ProcessInfo *out,
                                             size_t limit,
                                             size_t *out_count);

/* One page of every enumerated process, sorted like `top`. `page` is 1-based.
 * `page_size` is clamped to WT_PROCESS_PAGE_MAX. Identity is filled only for
 * the returned page. `out_count` is the number of rows copied into `out`. */
WT_Result wt_collect_process_page(WT_ProcessInfo *out,
                                  size_t out_cap,
                                  size_t page,
                                  size_t page_size,
                                  WT_ProcessSort sort,
                                  unsigned int sample_ms,
                                  int include_network,
                                  size_t *out_count,
                                  WT_ProcessPageInfo *page_info);

/* Applies PDH CPU and IO-counter deltas to an existing process list. */
WT_Result wt_enrich_process_metrics(WT_ProcessInfo *items,
                                    size_t count,
                                    unsigned int sample_ms);

#endif /* WINTUNE_PROCESS_H */
