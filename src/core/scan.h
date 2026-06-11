#ifndef WINTUNE_SCAN_H
#define WINTUNE_SCAN_H

#include <stddef.h>

#include "common/error.h"
#include "core/report_model.h"

typedef struct WT_ScanOptions {
    unsigned int cpu_sample_ms;   /* interval for the PDH CPU sample */
    size_t top_limit;             /* number of top processes to keep */
} WT_ScanOptions;

/* Runs a full local scan, populating `report`. Collection is best-effort:
 * sections that fail are flagged unavailable and the scan still returns WT_OK.
 * Pass NULL `opts` to use defaults. */
WT_Result wt_run_scan(const WT_ScanOptions *opts, WT_ScanReport *report);

#endif /* WINTUNE_SCAN_H */
