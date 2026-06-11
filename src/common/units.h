#ifndef WINTUNE_UNITS_H
#define WINTUNE_UNITS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Formats a byte count as a human-readable string (e.g. "2.4 GB"). Values below
 * 1 KB are printed as exact bytes; larger values use one decimal place. */
WT_Result wt_format_bytes(unsigned long long bytes, wchar_t *out, size_t count);

/* Formats a millisecond duration as a compact uptime string (e.g. "3d 04h"). */
WT_Result wt_format_duration_ms(unsigned long long ms, wchar_t *out, size_t count);

#endif /* WINTUNE_UNITS_H */
