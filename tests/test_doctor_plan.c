#include "core/doctor_plan.h"
#include "core/recommendations.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void expect_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        g_failed = 1;
    }
}

static void expect_int(int got, int want, const char *label)
{
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", label, got, want);
        g_failed = 1;
    }
}

static void fill_rec(WT_Recommendation *r, const char *id, const char *title,
                     const char *action, WT_Severity sev, int apply_style)
{
    memset(r, 0, sizeof(*r));
    strncpy_s(r->id, sizeof(r->id), id, _TRUNCATE);
    strncpy_s(r->title, sizeof(r->title), title, _TRUNCATE);
    strncpy_s(r->action, sizeof(r->action), action, _TRUNCATE);
    r->severity = sev;
    r->risk = WT_RISK_LOW;
    r->confidence_percent = 80;
    r->rollback_available = apply_style;
    (void)apply_style;
}

int main(void)
{
    WT_RecommendationList recs;
    memset(&recs, 0, sizeof(recs));

    fill_rec(&recs.items[0], "WT-STARTUP-002", "Disable heavy startup",
             "wintune apply WT-STARTUP-DISABLE \"HKCU\\Run:App\"",
             WT_SEVERITY_MEDIUM, 1);
    fill_rec(&recs.items[1], "WT-POWER-001", "Use High performance",
             "wintune apply WT-POWER-001", WT_SEVERITY_LOW, 1);
    fill_rec(&recs.items[2], "WT-MEMORY-001", "Memory pressure",
             "Close heavy apps or review startup", WT_SEVERITY_HIGH, 0);
    fill_rec(&recs.items[3], "WT-BLOCKER-001", "Apps blocking reboot",
             "wintune blockers", WT_SEVERITY_MEDIUM, 0);
    fill_rec(&recs.items[4], "WT-TASK-001", "Delay logon task",
             "wintune apply WT-TASK-DELAY \"task:\\Vendor\\App\" --seconds 30",
             WT_SEVERITY_LOW, 1);
    recs.count = 5;

    expect_true(wt_doctor_step_is_applyable(&recs.items[0]) == 1, "startup apply");
    expect_true(wt_doctor_step_is_applyable(&recs.items[1]) == 1, "power apply");
    expect_true(wt_doctor_step_is_applyable(&recs.items[2]) == 0, "memory review");
    expect_true(wt_doctor_step_is_applyable(&recs.items[3]) == 0, "blocker review");
    expect_true(wt_doctor_step_is_applyable(&recs.items[4]) == 1, "task apply");

    WT_DoctorPlan plan;
    expect_true(wt_doctor_plan_build(&recs, &plan) == WT_OK, "build ok");
    expect_int((int)plan.count, 5, "count");
    expect_int((int)plan.applyable_count, 3, "applyable");
    expect_int((int)plan.review_count, 2, "review");

    /* Order: blocker -> power -> memory -> startup -> task */
    expect_true(strcmp(plan.steps[0].rec_id, "WT-BLOCKER-001") == 0, "1st blocker");
    expect_true(strcmp(plan.steps[1].rec_id, "WT-POWER-001") == 0, "2nd power");
    expect_true(strcmp(plan.steps[2].rec_id, "WT-MEMORY-001") == 0, "3rd memory");
    expect_true(strcmp(plan.steps[3].rec_id, "WT-STARTUP-002") == 0, "4th startup");
    expect_true(strcmp(plan.steps[4].rec_id, "WT-TASK-001") == 0, "5th task");

    expect_true(plan.steps[1].depends_on[0] != '\0' &&
                    strcmp(plan.steps[1].depends_on, "WT-BLOCKER-001") == 0,
                "power after blocker");
    expect_true(plan.steps[3].depends_on[0] != '\0' &&
                    strcmp(plan.steps[3].depends_on, "WT-POWER-001") == 0,
                "startup after power");
    expect_true(plan.steps[4].depends_on[0] != '\0' &&
                    strcmp(plan.steps[4].depends_on, "WT-STARTUP-002") == 0,
                "task after startup");

    expect_true(wt_doctor_plan_build(NULL, &plan) == WT_ERR_INVALID_ARGUMENT,
                "null recs");

    if (g_failed) {
        fputs("doctor plan tests failed\n", stderr);
        return 1;
    }
    fputs("doctor plan tests passed\n", stdout);
    return 0;
}
