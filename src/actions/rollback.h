#ifndef WINTUNE_ROLLBACK_H
#define WINTUNE_ROLLBACK_H

#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#include "common/error.h"

/* Action type tokens stored in rollback records. Stable strings: do not
 * rename casually, older records on disk must keep working. */
#define WT_ROLLBACK_TYPE_POWER L"power_plan_change"

/* A single rollback record. Values are stored verbatim so an action can be
 * undone later. For a power plan change, previous_value/new_value hold the
 * canonical scheme GUID strings. */
typedef struct WT_RollbackRecord {
    wchar_t id[48];                 /* filename stem, time-based + unique */
    char    timestamp_utc[32];      /* ISO 8601, e.g. 2026-06-11T11:59:00Z */
    wchar_t action_type[64];        /* e.g. WT_ROLLBACK_TYPE_POWER */
    wchar_t action_id[64];          /* recommendation id, or "" */
    wchar_t description[256];       /* human-readable summary for listing */
    wchar_t previous_value[256];    /* value to restore on rollback */
    wchar_t new_value[256];         /* value applied by the action */
} WT_RollbackRecord;

/* Fills timestamp/id and writes the record as a JSON file under the per-user
 * rollback directory. On success, `rec->id` is set to the generated id.
 * Creating rollback data must never block the action itself: callers may log a
 * warning if this fails rather than aborting a successful change. */
WT_Result wt_rollback_write(WT_RollbackRecord *rec);

/* Lists all rollback records. When `json` is non-zero, emits a JSON array;
 * otherwise a human-readable table to `out`. */
WT_Result wt_rollback_list(FILE *out, int json);

/* Applies (undoes) the action recorded under `id`. Honors `assume_yes` for the
 * confirmation prompt. Returns WT_ERR_NOT_FOUND if the id has no record. */
WT_Result wt_rollback_apply(const wchar_t *id, int assume_yes);

#endif /* WINTUNE_ROLLBACK_H */
