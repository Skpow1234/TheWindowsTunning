#ifndef WINTUNE_SERVICE_POLICY_H
#define WINTUNE_SERVICE_POLICY_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Named WinTune service policy profiles (Phase 44).
 * Stored at %ProgramData%\WinTune\service_policy.json — explicit and removable. */

#define WT_SERVICE_POLICY_NAME_MAX 32

typedef struct WT_ServicePolicy {
    char name[WT_SERVICE_POLICY_NAME_MAX];
    unsigned scan_interval_ms;   /* 0 = on-demand only (no periodic timer) */
    unsigned sample_count;       /* multi-sample scan depth */
    unsigned top_process_limit;
    unsigned history_keep;       /* prior last_scan copies to retain (0..8) */
} WT_ServicePolicy;

/* Built-in profiles: balanced | performance | light | on-demand */
void wt_service_policy_defaults(WT_ServicePolicy *out); /* balanced */
WT_Result wt_service_policy_from_name(const wchar_t *name, WT_ServicePolicy *out);

WT_Result wt_service_policy_path(wchar_t *out, size_t count);
WT_Result wt_service_policy_load(WT_ServicePolicy *out);  /* file or balanced default */
WT_Result wt_service_policy_save(const WT_ServicePolicy *policy);
WT_Result wt_service_policy_remove(void); /* delete file; used on uninstall */

/* Rotate last_scan.json into history copies per policy->history_keep. */
WT_Result wt_service_policy_rotate_last_scan(const WT_ServicePolicy *policy);

const char *wt_service_policy_describe(const WT_ServicePolicy *policy);

#endif /* WINTUNE_SERVICE_POLICY_H */
