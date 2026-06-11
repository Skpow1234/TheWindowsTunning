#ifndef WINTUNE_RECOMMENDATIONS_H
#define WINTUNE_RECOMMENDATIONS_H

#include <stddef.h>

#include "common/error.h"
#include "core/report_model.h"

typedef enum WT_Severity {
    WT_SEVERITY_INFO = 0,
    WT_SEVERITY_LOW,
    WT_SEVERITY_MEDIUM,
    WT_SEVERITY_HIGH,
    WT_SEVERITY_CRITICAL
} WT_Severity;

typedef enum WT_Risk {
    WT_RISK_NONE = 0,
    WT_RISK_LOW,
    WT_RISK_MEDIUM,
    WT_RISK_HIGH
} WT_Risk;

typedef struct WT_Recommendation {
    char id[64];
    char title[128];
    char reason[512];
    char action[256];
    WT_Severity severity;
    WT_Risk risk;
    int requires_admin;
    int rollback_available;
    int confidence_percent;
} WT_Recommendation;

#define WT_MAX_RECOMMENDATIONS 32

typedef struct WT_RecommendationList {
    WT_Recommendation items[WT_MAX_RECOMMENDATIONS];
    size_t count;
} WT_RecommendationList;

/* Produces deterministic, explainable recommendations from a scan report.
 * Same inputs always yield the same output. Never recommends dangerous
 * actions. */
WT_Result wt_generate_recommendations(const WT_ScanReport *report,
                                      WT_RecommendationList *out);

const char *wt_severity_to_string(WT_Severity severity);
const char *wt_risk_to_string(WT_Risk risk);

#endif /* WINTUNE_RECOMMENDATIONS_H */
