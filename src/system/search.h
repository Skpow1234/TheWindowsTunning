#ifndef WINTUNE_SEARCH_H
#define WINTUNE_SEARCH_H

#include "common/error.h"

/* Read-only Windows Search catalog snapshot. Counts are -1 when unread.
 * WinTune does not reset, reindex, or disable Search. */
typedef struct WT_SearchReport {
    int available;
    int service_running; /* 1 running, 0 stopped, -1 unread */
    int status_ok;
    int status;          /* CatalogStatus, or -1 */
    int paused_reason;   /* CatalogPausedReason when paused, else -1 */
    int items_ok;
    long items_indexed;
    int backlog_ok;
    long incremental_count;
    long notification_count;
    long high_priority_count;
    long backlog_items;
} WT_SearchReport;

WT_Result wt_collect_search(WT_SearchReport *out);

const char *wt_search_status_token(int status);
const char *wt_search_pause_token(int reason);

/* True for a full crawl, catalog recovery, or a backlog of at least 100. */
int wt_search_backlog_notable(const WT_SearchReport *report);

#endif /* WINTUNE_SEARCH_H */
