#ifndef WINTUNE_APPLY_H
#define WINTUNE_APPLY_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Applies the safe action associated with a recommendation id (e.g.
 * "WT-POWER-001"). Actionable recommendations are dispatched to the matching
 * safe action (with confirmation + rollback). Advisory recommendations have no
 * automatic action and report that clearly. Dangerous actions are never
 * applied. `msg` receives a human-readable result line. */
WT_Result wt_apply_recommendation(const wchar_t *id,
                                  int assume_yes,
                                  char *msg, size_t msg_cap);

#endif /* WINTUNE_APPLY_H */
