#ifndef WINTUNE_APPLY_H
#define WINTUNE_APPLY_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "actions/action_map.h"

WT_Result wt_apply_recommendation_ex(const WT_ApplyRequest *req);

WT_Result wt_apply_recommendation(const wchar_t *id,
                                  const wchar_t *target_id,
                                  unsigned long delay_seconds,
                                  int assume_yes,
                                  char *msg, size_t msg_cap);

#endif /* WINTUNE_APPLY_H */
