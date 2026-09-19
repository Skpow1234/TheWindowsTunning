#ifndef WINTUNE_DOCTOR_PLAN_H
#define WINTUNE_DOCTOR_PLAN_H

#include <stddef.h>
#include <stdio.h>

#include "common/error.h"
#include "core/recommendations.h"

#define WT_MAX_DOCTOR_STEPS WT_MAX_RECOMMENDATIONS

typedef struct WT_DoctorPlanStep {
    int order; /* 1-based sequence */
    char rec_id[64];
    char title[128];
    char command[256];
    char depends_on[64]; /* prior step rec_id, or empty */
    char note[192];
    WT_Severity severity;
    WT_Risk risk;
    int requires_admin;
    int rollback_available;
    int confidence_percent;
    int applyable; /* 1 = safe apply path exists; still needs confirm */
} WT_DoctorPlanStep;

typedef struct WT_DoctorPlan {
    WT_DoctorPlanStep steps[WT_MAX_DOCTOR_STEPS];
    size_t count;
    size_t applyable_count;
    size_t review_count;
} WT_DoctorPlan;

void wt_doctor_plan_init(WT_DoctorPlan *plan);

/* Builds an ordered checklist from recommendations. Read-only; never applies. */
WT_Result wt_doctor_plan_build(const WT_RecommendationList *recs,
                               WT_DoctorPlan *out);

void wt_doctor_plan_print_text(FILE *out, const WT_DoctorPlan *plan);
void wt_doctor_plan_print_json(FILE *out, const WT_DoctorPlan *plan);

/* True when the recommendation has a confirmed-apply path (not advisory-only). */
int wt_doctor_step_is_applyable(const WT_Recommendation *rec);

#endif /* WINTUNE_DOCTOR_PLAN_H */
