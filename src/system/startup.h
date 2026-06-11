#ifndef WINTUNE_STARTUP_H
#define WINTUNE_STARTUP_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

typedef enum WT_StartupSource {
    WT_STARTUP_SRC_HKCU_RUN = 0,
    WT_STARTUP_SRC_HKLM_RUN,
    WT_STARTUP_SRC_HKCU_RUNONCE,
    WT_STARTUP_SRC_HKLM_RUNONCE,
    WT_STARTUP_SRC_USER_FOLDER,
    WT_STARTUP_SRC_COMMON_FOLDER
} WT_StartupSource;

typedef enum WT_StartupImpact {
    WT_STARTUP_IMPACT_UNKNOWN = 0,
    WT_STARTUP_IMPACT_LOW,
    WT_STARTUP_IMPACT_MEDIUM,
    WT_STARTUP_IMPACT_HIGH
} WT_StartupImpact;

typedef struct WT_StartupEntry {
    wchar_t id[160];        /* synthetic, stable id: "<source>:<name>" */
    wchar_t name[128];      /* value name or file name */
    wchar_t command[1024];  /* command line / target path (env-expanded) */
    WT_StartupSource source;
    int enabled;            /* read-only: present entries are treated as enabled */
    WT_StartupImpact impact;
} WT_StartupEntry;

#define WT_MAX_STARTUP_ENTRIES 256

/* Scans the per-user/per-machine Run and RunOnce registry keys and the user and
 * common Startup folders. Read-only. Writes up to `capacity` entries and
 * reports the count via `out_count`. Partial sources that fail (e.g. an
 * inaccessible HKLM key) are skipped rather than aborting the whole scan. */
WT_Result wt_collect_startup_entries(WT_StartupEntry *out,
                                     size_t capacity,
                                     size_t *out_count);

const char *wt_startup_source_name(WT_StartupSource source);
const char *wt_startup_impact_name(WT_StartupImpact impact);

#endif /* WINTUNE_STARTUP_H */
