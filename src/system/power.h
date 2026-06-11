#ifndef WINTUNE_POWER_H
#define WINTUNE_POWER_H

#include <wchar.h>

#include "common/error.h"

typedef enum WT_PowerScheme {
    WT_POWER_UNKNOWN = 0,
    WT_POWER_BALANCED,
    WT_POWER_HIGH_PERF,
    WT_POWER_POWER_SAVER,
    WT_POWER_ULTIMATE
} WT_PowerScheme;

typedef struct WT_PowerInfo {
    WT_PowerScheme scheme;
    wchar_t active_name[128];   /* friendly name as reported by Windows */
    int on_ac;                  /* 1 = AC, 0 = battery, -1 = unknown */
    int battery_percent;        /* 0..100, or -1 when unknown / no battery */
} WT_PowerInfo;

/* Reads the active power scheme (matched by GUID, locale-independent) and the
 * current power source. Read-only; never changes any setting. */
WT_Result wt_collect_power_info(WT_PowerInfo *out);

/* Canonical English name for a scheme (locale-independent), for output and
 * recommendations. Never NULL. */
const char *wt_power_scheme_name(WT_PowerScheme scheme);

#endif /* WINTUNE_POWER_H */
