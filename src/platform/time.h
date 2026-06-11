#ifndef WINTUNE_TIME_H
#define WINTUNE_TIME_H

#include <stddef.h>

#include "common/error.h"

/* Writes the current UTC time as an ISO-8601 string (e.g.
 * "2026-06-11T07:54:00Z"). Needs at least 21 bytes. */
WT_Result wt_now_iso8601_utc(char *out, size_t count);

#endif /* WINTUNE_TIME_H */
