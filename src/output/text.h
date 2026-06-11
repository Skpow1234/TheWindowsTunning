#ifndef WINTUNE_TEXT_H
#define WINTUNE_TEXT_H

#include "core/report_model.h"
#include "core/recommendations.h"

/* Prints a full scan report as human-readable text to stdout. When `recs` is
 * non-NULL, a Recommendations section is appended. */
void wt_print_scan_report_text(const WT_ScanReport *report,
                               const WT_RecommendationList *recs);

/* Prints just the Recommendations section to stdout. */
void wt_print_recommendations_text(const WT_RecommendationList *recs);

#endif /* WINTUNE_TEXT_H */
