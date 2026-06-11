#ifndef WINTUNE_TEXT_REPORT_H
#define WINTUNE_TEXT_REPORT_H

#include <stdio.h>

#include "core/report_model.h"
#include "core/recommendations.h"

/* Writes a full performance report (extended beyond the scan summary) to `out`.
 * Includes system/performance summaries, derived bottlenecks, recommendations,
 * and risk notes. Read-only output. */
void wt_print_performance_report_text(FILE *out,
                                      const WT_ScanReport *report,
                                      const WT_RecommendationList *recs);

#endif /* WINTUNE_TEXT_REPORT_H */
