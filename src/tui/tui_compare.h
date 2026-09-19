#ifndef WINTUNE_TUI_COMPARE_H
#define WINTUNE_TUI_COMPARE_H

#include <stddef.h>
#include <stdio.h>

#include "common/error.h"

/* In-session / on-disk before/after metric snapshots (Phase 41).
 * Deltas are measured sample differences only — never marketed as lasting gains. */

typedef struct WT_TuiCompareSnap {
    int valid;
    char captured_utc[32];
    double cpu_percent;              /* -1 if unknown */
    double mem_percent;              /* -1 if unknown */
    double disk_percent;             /* -1 if unknown */
    double net_rx_bps;               /* -1 if unknown */
    double net_tx_bps;               /* -1 if unknown */
    unsigned long long mem_used_bytes;
    unsigned long long mem_total_bytes;
} WT_TuiCompareSnap;

typedef struct WT_TuiComparePair {
    WT_TuiCompareSnap before;
    WT_TuiCompareSnap after;
} WT_TuiComparePair;

typedef struct WT_TuiCompareDelta {
    int both_valid;
    double cpu_pp;                   /* after − before (percentage points) */
    double mem_pp;
    double disk_pp;
    double net_rx_bps;
    double net_tx_bps;
    long long mem_used_bytes;
} WT_TuiCompareDelta;

void wt_tui_compare_snap_clear(WT_TuiCompareSnap *s);

void wt_tui_compare_capture(WT_TuiCompareSnap *out,
                            double cpu, double mem_pct, double disk,
                            double net_rx, double net_tx, int net_ok,
                            unsigned long long mem_used,
                            unsigned long long mem_total, int mem_ok);

void wt_tui_compare_delta(const WT_TuiComparePair *pair,
                          WT_TuiCompareDelta *out);

/* Persist under %LOCALAPPDATA%\WinTune\compare\{before|after}.json */
WT_Result wt_tui_compare_save_slot(const wchar_t *slot,
                                   const WT_TuiCompareSnap *snap);
WT_Result wt_tui_compare_load_slot(const wchar_t *slot,
                                   WT_TuiCompareSnap *out);
WT_Result wt_tui_compare_load_pair(WT_TuiComparePair *out);

/* Human-readable compare block for TUI / report / file. */
void wt_tui_compare_print(FILE *out, const WT_TuiComparePair *pair);

#endif /* WINTUNE_TUI_COMPARE_H */
