#ifndef WINTUNE_PDH_UTILS_H
#define WINTUNE_PDH_UTILS_H

#include "common/error.h"

#include <windows.h>
#include <pdh.h>

/* Samples a single PDH counter over a short interval and returns its formatted
 * double value. Two collections are required for rate counters (such as
 * "% Processor Time"), so the call blocks for `interval_ms` between them.
 *
 * English counter paths are used (via PdhAddEnglishCounterW) so the same paths
 * work regardless of the system's display language. */
WT_Result wt_pdh_sample_single(const wchar_t *counter_path,
                               unsigned int interval_ms,
                               double *out_value);

/* Builds a PID -> CPU% map using PDH Process counters (% Processor Time and
 * ID Process). Requires two query collections separated by `interval_ms`.
 * Only the first `capacity` entries are written; `out_count` is the number
 * stored. Processes that cannot be read are omitted. */
WT_Result wt_pdh_collect_process_cpu(unsigned int interval_ms,
                                     unsigned long *pids,
                                     double *cpu_percent,
                                     size_t capacity,
                                     size_t *out_count);

/* Keeps PDH queries open across the samples of one scan. Calls outside a
 * scan still open and close their own queries. begin/end may nest. */
void wt_pdh_cache_begin(void);
void wt_pdh_cache_end(void);
int wt_pdh_cache_active(void);
unsigned wt_pdh_cache_generation(void);

/* Remember `query` until the matching cache end. Returns 0 when the cache
 * is inactive or full; the caller then owns the query. */
int wt_pdh_cache_track(PDH_HQUERY query);

/* Close `query` and drop it from the cache. Returns 0 if it was not tracked. */
int wt_pdh_cache_forget(PDH_HQUERY query);

#endif /* WINTUNE_PDH_UTILS_H */
