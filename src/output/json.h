#ifndef WINTUNE_JSON_H
#define WINTUNE_JSON_H

#include <stddef.h>
#include <stdio.h>

#include "core/report_model.h"
#include "core/recommendations.h"
#include "metrics/process.h"
#include "system/startup.h"
#include "system/tasks.h"
#include "system/services.h"
#include "system/boot.h"
#include "system/updates.h"
#include "system/blockers.h"
#include "system/storage_health.h"
#include "system/reliability.h"

/* Stable JSON schema version (independent of wintune tool version).
 * 2.0.0 documents accumulated additive fields (Phases 18–46) and adds
 * envelope metadata. Core 1.0 keys remain stable — see schema_compat_min. */
#define WT_JSON_SCHEMA_VERSION "2.0.0"
#define WT_JSON_SCHEMA_COMPAT_MIN "1.0.0"
#define WT_JSON_SCHEMA_VERSION_V1 "1.0.0"

struct WT_CliOptions;

/* Minimal, dependency-free JSON writer that emits pretty-printed UTF-8 JSON to
 * a FILE stream. It tracks nesting and comma placement so callers only describe
 * structure. Strings are escaped; wide strings are converted to UTF-8. */
#define WT_JSON_MAX_DEPTH 32

typedef struct WT_JsonWriter {
    FILE *out;
    int depth;
    int counts[WT_JSON_MAX_DEPTH]; /* members written per nesting level */
    int expect_value;              /* 1 after a key, before its value */
    int compact;                   /* minified output when non-zero */
} WT_JsonWriter;

void wt_json_init(WT_JsonWriter *w, FILE *out);
void wt_json_set_compact(WT_JsonWriter *w, int compact);

/* Applies --compact-json / --ndjson / --schema-version from CLI options. */
void wt_json_apply_cli_options(const struct WT_CliOptions *opts);

/* Emit major: 1 = legacy pin (deprecation window), 2 = current default. */
void wt_json_set_emit_major(int major);
int wt_json_emit_major(void);
const char *wt_json_emit_schema_version(void);

/* Writes schema_version and, for v2+, schema_compat_min + optional document. */
void wt_json_emit_schema_meta(WT_JsonWriter *w, const char *document);
void wt_json_begin_object(WT_JsonWriter *w);
void wt_json_end_object(WT_JsonWriter *w);
void wt_json_begin_array(WT_JsonWriter *w);
void wt_json_end_array(WT_JsonWriter *w);
void wt_json_key(WT_JsonWriter *w, const char *key);
void wt_json_string(WT_JsonWriter *w, const char *utf8);
void wt_json_wstring(WT_JsonWriter *w, const wchar_t *ws);
void wt_json_uint64(WT_JsonWriter *w, unsigned long long value);
void wt_json_double(WT_JsonWriter *w, double value);
void wt_json_bool(WT_JsonWriter *w, int value);
void wt_json_null(WT_JsonWriter *w);
void wt_json_finish(WT_JsonWriter *w); /* trailing newline */

/* High-level document emitters (the stable WinTune JSON schema). `recs` may be
 * NULL, in which case the recommendations array is emitted empty. */
void wt_print_scan_report_json(const WT_ScanReport *report,
                               const WT_RecommendationList *recs, FILE *out);
void wt_print_processes_json(const WT_ProcessInfo *items, size_t count, FILE *out);
void wt_print_recommendations_json(const WT_RecommendationList *recs, FILE *out);
void wt_print_startup_json(const WT_StartupEntry *items, size_t count, FILE *out);
void wt_print_tasks_json(const WT_ScheduledTask *items, size_t count, FILE *out);
void wt_print_services_json(const WT_ServiceInfo *items, size_t count, FILE *out);
void wt_print_boot_json(const WT_BootReport *boot, FILE *out);
void wt_print_updates_json(const WT_UpdateStatus *status, FILE *out);
void wt_print_blockers_json(const WT_BlockerReport *report, FILE *out);

void wt_print_storage_json(const WT_StorageHealthReport *report, FILE *out);
void wt_print_reliability_json(const WT_ReliabilityReport *report, FILE *out);

#endif /* WINTUNE_JSON_H */
