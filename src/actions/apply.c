#include "actions/apply.h"
#include "actions/action_map.h"

#include <strsafe.h>

WT_Result wt_apply_recommendation_ex(const WT_ApplyRequest *req)
{
    return wt_apply_from_request(req);
}

WT_Result wt_apply_recommendation(const wchar_t *id,
                                  const wchar_t *target_id,
                                  unsigned long delay_seconds,
                                  int assume_yes,
                                  char *msg, size_t msg_cap)
{
    WT_ApplyRequest req = {
        .rec_id = id,
        .target_id = target_id,
        .delay_seconds = delay_seconds,
        .assume_yes = assume_yes,
        .msg = msg,
        .msg_cap = msg_cap,
    };
    return wt_apply_from_request(&req);
}
