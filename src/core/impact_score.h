#ifndef WINTUNE_IMPACT_SCORE_H
#define WINTUNE_IMPACT_SCORE_H

#include <stddef.h>

#include "system/startup.h"
#include "system/tasks.h"
#include "system/file_identity.h"
#include "metrics/process.h"

/* Evidence-based impact for a single startup item or task — not a fake
 * "PC health" percentage. Higher score = more likely login/boot cost. */

#define WT_IMPACT_EV_MEASURED   0x01u
#define WT_IMPACT_EV_HEURISTIC  0x02u
#define WT_IMPACT_EV_PUBLISHER  0x04u
#define WT_IMPACT_EV_LOCATION   0x08u
#define WT_IMPACT_EV_RUNTIME    0x10u

/* Recommend disable/delay review when score reaches this (and confidence OK). */
#define WT_IMPACT_RECOMMEND_SCORE_MIN 65
#define WT_IMPACT_RECOMMEND_CONF_MIN  50

typedef struct WT_ImpactInput {
    const wchar_t *name;
    const wchar_t *command;
    unsigned long measured_ms;
    int measured_available;
    WT_PublisherOrigin origin;
    int unusual_location;
    int microsoft_protected; /* scheduled-task Microsoft protection bit */
    double cpu_percent;                   /* -1.0 when not matched */
    unsigned long long working_set_bytes; /* 0 when not matched */
    double disk_bytes_per_sec;            /* -1.0 when not matched */
} WT_ImpactInput;

typedef struct WT_ImpactScore {
    int score;                 /* 0–100 item impact */
    WT_StartupImpact band;     /* low / medium / high / unknown */
    int confidence_percent;    /* how much evidence contributed */
    unsigned int evidence;     /* WT_IMPACT_EV_* bits */
} WT_ImpactScore;

void wt_impact_score_compute(const WT_ImpactInput *in, WT_ImpactScore *out);
WT_StartupImpact wt_impact_score_to_band(int score, unsigned int evidence);
int wt_impact_name_heuristic_hit(const wchar_t *name, const wchar_t *command);

void wt_impact_input_from_startup(const WT_StartupEntry *e, WT_ImpactInput *out);
void wt_impact_apply_to_startup(WT_StartupEntry *e, const WT_ImpactScore *score);

void wt_impact_input_from_task(const WT_ScheduledTask *t, WT_ImpactInput *out);
void wt_impact_apply_to_task(WT_ScheduledTask *t, const WT_ImpactScore *score);

void wt_impact_attach_runtime(WT_ImpactInput *in,
                              const WT_ProcessInfo *procs,
                              size_t count);

#endif /* WINTUNE_IMPACT_SCORE_H */
