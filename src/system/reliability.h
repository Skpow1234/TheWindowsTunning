#ifndef WINTUNE_RELIABILITY_H
#define WINTUNE_RELIABILITY_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Read-only reliability signals from Event Log + local dump/WER metadata.
 * Never opens dump contents, never uploads, never "fixes" corruption. */

#define WT_RELIABILITY_LOOKBACK_DAYS 14
#define WT_MAX_RELIABILITY_EVENTS    24
#define WT_MAX_CRASH_APPS            12
#define WT_MAX_DUMP_META             16

typedef enum WT_ReliabilityKind {
    WT_REL_UNKNOWN = 0,
    WT_REL_KERNEL_POWER,       /* System 41 */
    WT_REL_UNEXPECTED_SHUTDOWN,/* System 6008 */
    WT_REL_BUGCHECK,           /* System 1001 WER bugcheck */
    WT_REL_APP_CRASH,          /* Application 1000 */
    WT_REL_APP_HANG,           /* Application 1002 */
    WT_REL_WER_REPORT          /* Application 1001 */
} WT_ReliabilityKind;

typedef struct WT_ReliabilityEvent {
    WT_ReliabilityKind kind;
    unsigned event_id;
    char time_utc[32];
    wchar_t detail[128];
} WT_ReliabilityEvent;

typedef struct WT_ReliabilityCrashApp {
    wchar_t name[96];
    unsigned count;
} WT_ReliabilityCrashApp;

typedef struct WT_ReliabilityDumpMeta {
    wchar_t name[128];
    wchar_t location[48]; /* "minidump", "memory.dmp", "wer_archive", ... */
    unsigned long long size_bytes;
    char modified_utc[32];
    int is_directory;
} WT_ReliabilityDumpMeta;

typedef struct WT_ReliabilityReport {
    int lookback_days;
    unsigned kernel_power_count;
    unsigned unexpected_shutdown_count;
    unsigned bugcheck_count;
    unsigned app_crash_count;
    unsigned app_hang_count;
    unsigned wer_report_count;

    char last_unexpected_utc[32];
    char last_bugcheck_utc[32];
    wchar_t last_bugcheck_detail[64];

    size_t recent_count;
    WT_ReliabilityEvent recent[WT_MAX_RELIABILITY_EVENTS];

    size_t crash_app_count;
    WT_ReliabilityCrashApp crash_apps[WT_MAX_CRASH_APPS];

    size_t dump_meta_count;
    WT_ReliabilityDumpMeta dumps[WT_MAX_DUMP_META];

    int events_ok;
    int dumps_ok;
    wchar_t note[288];
} WT_ReliabilityReport;

void wt_reliability_init(WT_ReliabilityReport *report);

/* Collects recent Event Log signals + dump/WER folder metadata only. */
WT_Result wt_collect_reliability(WT_ReliabilityReport *report);

const char *wt_reliability_kind_name(WT_ReliabilityKind kind);

#endif /* WINTUNE_RELIABILITY_H */
