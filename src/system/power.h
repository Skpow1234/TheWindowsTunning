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

/* Windows 11 power-mode slider (overlay). Distinct from the power plan. */
typedef enum WT_PowerMode {
    WT_POWER_MODE_UNKNOWN = 0,
    WT_POWER_MODE_BEST_EFFICIENCY,
    WT_POWER_MODE_BALANCED,
    WT_POWER_MODE_BETTER_PERFORMANCE,
    WT_POWER_MODE_BEST_PERFORMANCE
} WT_PowerMode;

typedef struct WT_PowerInfo {
    WT_PowerScheme scheme;
    wchar_t active_name[128];   /* friendly name as reported by Windows */
    int on_ac;                  /* 1 = AC, 0 = battery, -1 = unknown */
    int battery_percent;        /* 0..100, or -1 when unknown / no battery */

    /* Phase 29 — thermal / power budget (read-only). */
    int battery_present;        /* 1/0/-1 */
    int charging;               /* 1/0/-1 */
    int discharging;            /* 1/0/-1 */
    int rate_mw;                /* signed mW; negative = discharge; 0 unknown */
    int rate_ok;
    int estimated_seconds;      /* seconds remaining at current rate; -1 n/a */
    int remaining_mwh;          /* -1 when unknown */
    int full_mwh;               /* -1 when unknown */
    int processor_max_pct_ac;   /* plan PROCTHROTTLEMAX on AC; -1 n/a */
    int processor_max_pct_dc;   /* plan PROCTHROTTLEMAX on DC; -1 n/a */
    int processor_capped;       /* 1 when active source plan max < 100 */

    /* Phase 61 — Windows 11 power mode (read-only; never changed here). */
    WT_PowerMode mode_ac;
    WT_PowerMode mode_dc;
    int mode_ac_ok;             /* 1 when the AC slider was read */
    int mode_dc_ok;             /* 1 when the DC slider was read */
} WT_PowerInfo;

/* Reads the active power scheme (matched by GUID, locale-independent), power
 * source, battery budget (SYSTEM_BATTERY_STATE), plan processor caps, and the
 * Windows 11 power-mode slider for AC and battery. Read-only; never changes
 * the plan or the power mode. */
WT_Result wt_collect_power_info(WT_PowerInfo *out);

/* Stable JSON token for a power mode. "unknown" when the GUID was read but
 * is not one of the public modes. Never NULL. */
const char *wt_power_mode_token(WT_PowerMode mode);

/* English label for text output. Never NULL. */
const char *wt_power_mode_label(WT_PowerMode mode);

/* Power mode for the current source. *ok is 1 when that side was read. */
WT_PowerMode wt_power_active_mode(const WT_PowerInfo *info, int *ok);

/* Canonical English name for a scheme (locale-independent), for output and
 * recommendations. Never NULL. */
const char *wt_power_scheme_name(WT_PowerScheme scheme);

/* Maps a CLI token ("balanced", "performance"/"high", "saver", "ultimate") to
 * a scheme enum. Returns WT_POWER_UNKNOWN for unrecognized tokens. */
WT_PowerScheme wt_power_scheme_from_token(const wchar_t *token);

/* Writes the active scheme's GUID as a canonical "{8-4-4-4-12}" string.
 * Read-only. */
WT_Result wt_power_get_active_guid_string(wchar_t *out, size_t count);

/* Writes the canonical GUID string for a standard scheme (does not activate).
 * Read-only. Returns WT_ERR_INVALID_ARGUMENT for WT_POWER_UNKNOWN. */
WT_Result wt_power_scheme_guid_string(WT_PowerScheme scheme,
                                      wchar_t *out, size_t count);

/* Switches the active power scheme to one of the standard schemes. This is a
 * mutating action: it changes the system's active power plan via the official
 * Power Management API. Returns WT_ERR_NOT_FOUND if the requested scheme does
 * not exist on this machine (e.g. High performance hidden by OEM policy). */
WT_Result wt_power_set_active_scheme(WT_PowerScheme scheme);

/* Switches the active power scheme by canonical GUID string. Used by rollback
 * to restore a previously active scheme exactly (including custom plans). */
WT_Result wt_power_set_active_guid_string(const wchar_t *guid_str);

#endif /* WINTUNE_POWER_H */
