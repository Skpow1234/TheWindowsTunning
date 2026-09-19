#ifndef WINTUNE_DELAY_PLAN_H
#define WINTUNE_DELAY_PLAN_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "system/file_identity.h"

#define WT_MAX_DELAY_PLAN_ITEMS 64
#define WT_DELAY_PLAN_DEFAULT_BASE_SEC 30u
#define WT_DELAY_PLAN_STAGGER_SEC 15u
#define WT_DELAY_PLAN_MAX_SEC 300u

typedef enum WT_DelayPlanKind {
    WT_DELAY_KIND_STARTUP = 0,
    WT_DELAY_KIND_TASK
} WT_DelayPlanKind;

typedef struct WT_DelayPlanItem {
    wchar_t id[280];
    wchar_t name[128];
    WT_DelayPlanKind kind;
    unsigned long delay_seconds; /* 0 when excluded */
    int recommended;             /* 1 = included in apply set */
    int excluded;
    char exclude_reason[96];
    int impact_score;
    unsigned long measured_ms;
    WT_PublisherOrigin origin;
} WT_DelayPlanItem;

typedef struct WT_DelayPlan {
    WT_DelayPlanItem items[WT_MAX_DELAY_PLAN_ITEMS];
    size_t count;
    size_t recommended_count;
    size_t excluded_count;
    unsigned long base_seconds;
    unsigned long stagger_seconds;
    int include_tasks;
} WT_DelayPlan;

void wt_delay_plan_init(WT_DelayPlan *plan);

/* True for Microsoft security / health startups that must never be auto-delayed. */
int wt_delay_plan_is_security_blocked(const wchar_t *name,
                                      const wchar_t *command,
                                      const WT_FileIdentity *identity);

/* Build a preview plan from current startup (+ optional logon tasks). */
WT_Result wt_delay_plan_build(WT_DelayPlan *plan,
                              unsigned long base_seconds,
                              int include_tasks);

/* Apply recommended items. Per-item confirm unless assume_yes.
 * Writes rollback records via existing delay actions. */
WT_Result wt_delay_plan_apply(const WT_DelayPlan *plan,
                              int assume_yes,
                              char *msg,
                              size_t msg_cap);

const char *wt_delay_plan_kind_name(WT_DelayPlanKind kind);

#endif /* WINTUNE_DELAY_PLAN_H */
