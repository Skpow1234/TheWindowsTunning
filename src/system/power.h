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

/* Maps a CLI token ("balanced", "performance"/"high", "saver", "ultimate") to
 * a scheme enum. Returns WT_POWER_UNKNOWN for unrecognized tokens. */
WT_PowerScheme wt_power_scheme_from_token(const wchar_t *token);

/* Writes the active scheme's GUID as a canonical "{8-4-4-4-12}" string.
 * Read-only. */
WT_Result wt_power_get_active_guid_string(wchar_t *out, size_t count);

/* Switches the active power scheme to one of the standard schemes. This is a
 * mutating action: it changes the system's active power plan via the official
 * Power Management API. Returns WT_ERR_NOT_FOUND if the requested scheme does
 * not exist on this machine (e.g. High performance hidden by OEM policy). */
WT_Result wt_power_set_active_scheme(WT_PowerScheme scheme);

/* Switches the active power scheme by canonical GUID string. Used by rollback
 * to restore a previously active scheme exactly (including custom plans). */
WT_Result wt_power_set_active_guid_string(const wchar_t *guid_str);

#endif /* WINTUNE_POWER_H */
