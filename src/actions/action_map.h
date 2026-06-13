#ifndef WINTUNE_ACTION_MAP_H
#define WINTUNE_ACTION_MAP_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "system/power.h"

typedef enum WT_ApplyKind {
    WT_APPLY_NONE = 0,
    WT_APPLY_POWER_SCHEME,
    WT_APPLY_STARTUP_DISABLE,
    WT_APPLY_STARTUP_DELAY,
    WT_APPLY_TASK_DISABLE,
    WT_APPLY_TASK_DELAY,
    WT_APPLY_ADVISORY
} WT_ApplyKind;

typedef struct WT_ApplySpec {
    const wchar_t *rec_id;
    WT_ApplyKind kind;
    WT_PowerScheme power_scheme;
    unsigned long default_delay_seconds;
} WT_ApplySpec;

typedef struct WT_ApplyRequest {
    const wchar_t *rec_id;
    const wchar_t *target_id;
    unsigned long delay_seconds;
    int assume_yes;
    char *msg;
    size_t msg_cap;
} WT_ApplyRequest;

const WT_ApplySpec *wt_apply_spec_lookup(const wchar_t *rec_id);
int wt_apply_is_advisory_rec_id(const wchar_t *rec_id);
size_t wt_apply_spec_count(void);
const WT_ApplySpec *wt_apply_spec_at(size_t index);

WT_Result wt_apply_from_request(const WT_ApplyRequest *req);

#endif /* WINTUNE_ACTION_MAP_H */
