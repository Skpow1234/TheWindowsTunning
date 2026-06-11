#include "output/json.h"

#include "cli/cli.h"        /* WT_VERSION_STRING */
#include "platform/time.h"
#include "system/power.h"

#include <windows.h>

/* ------------------------------------------------------------------ */
/* Low-level writer                                                    */
/* ------------------------------------------------------------------ */

void wt_json_init(WT_JsonWriter *w, FILE *out)
{
    w->out = out;
    w->depth = 0;
    w->expect_value = 0;
    for (int i = 0; i < WT_JSON_MAX_DEPTH; ++i) {
        w->counts[i] = 0;
    }
}

static void wt_json_newline_indent(WT_JsonWriter *w, int level)
{
    fputc('\n', w->out);
    for (int i = 0; i < level * 2; ++i) {
        fputc(' ', w->out);
    }
}

/* Prefix for a value that is an array element or top-level value. After a key,
 * the value follows ": " directly and no prefix is needed. */
static void wt_json_value_prefix(WT_JsonWriter *w)
{
    if (w->expect_value) {
        w->expect_value = 0;
        return;
    }
    if (w->depth > 0) {
        if (w->counts[w->depth - 1] > 0) {
            fputc(',', w->out);
        }
        wt_json_newline_indent(w, w->depth);
        w->counts[w->depth - 1]++;
    }
}

static void wt_json_write_escaped(WT_JsonWriter *w, const char *s)
{
    fputc('"', w->out);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p) {
        unsigned char c = *p;
        switch (c) {
        case '"':  fputs("\\\"", w->out); break;
        case '\\': fputs("\\\\", w->out); break;
        case '\b': fputs("\\b", w->out);  break;
        case '\f': fputs("\\f", w->out);  break;
        case '\n': fputs("\\n", w->out);  break;
        case '\r': fputs("\\r", w->out);  break;
        case '\t': fputs("\\t", w->out);  break;
        default:
            if (c < 0x20) {
                fprintf(w->out, "\\u%04x", c);
            } else {
                fputc((int)c, w->out);
            }
            break;
        }
    }
    fputc('"', w->out);
}

void wt_json_begin_object(WT_JsonWriter *w)
{
    wt_json_value_prefix(w);
    fputc('{', w->out);
    if (w->depth < WT_JSON_MAX_DEPTH) {
        w->depth++;
        w->counts[w->depth - 1] = 0;
    }
}

void wt_json_end_object(WT_JsonWriter *w)
{
    int members = 0;
    if (w->depth > 0) {
        members = w->counts[w->depth - 1];
        w->depth--;
    }
    if (members > 0) {
        wt_json_newline_indent(w, w->depth);
    }
    fputc('}', w->out);
}

void wt_json_begin_array(WT_JsonWriter *w)
{
    wt_json_value_prefix(w);
    fputc('[', w->out);
    if (w->depth < WT_JSON_MAX_DEPTH) {
        w->depth++;
        w->counts[w->depth - 1] = 0;
    }
}

void wt_json_end_array(WT_JsonWriter *w)
{
    int members = 0;
    if (w->depth > 0) {
        members = w->counts[w->depth - 1];
        w->depth--;
    }
    if (members > 0) {
        wt_json_newline_indent(w, w->depth);
    }
    fputc(']', w->out);
}

void wt_json_key(WT_JsonWriter *w, const char *key)
{
    if (w->depth > 0) {
        if (w->counts[w->depth - 1] > 0) {
            fputc(',', w->out);
        }
        wt_json_newline_indent(w, w->depth);
        w->counts[w->depth - 1]++;
    }
    wt_json_write_escaped(w, key);
    fputs(": ", w->out);
    w->expect_value = 1;
}

void wt_json_string(WT_JsonWriter *w, const char *utf8)
{
    wt_json_value_prefix(w);
    wt_json_write_escaped(w, utf8 != NULL ? utf8 : "");
}

void wt_json_wstring(WT_JsonWriter *w, const wchar_t *ws)
{
    char buffer[1024];
    if (ws == NULL) {
        ws = L"";
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, buffer, sizeof(buffer),
                                NULL, NULL);
    if (n <= 0) {
        buffer[0] = '\0';
    }
    wt_json_value_prefix(w);
    wt_json_write_escaped(w, buffer);
}

void wt_json_uint64(WT_JsonWriter *w, unsigned long long value)
{
    wt_json_value_prefix(w);
    fprintf(w->out, "%llu", value);
}

void wt_json_double(WT_JsonWriter *w, double value)
{
    wt_json_value_prefix(w);
    fprintf(w->out, "%.1f", value);
}

void wt_json_bool(WT_JsonWriter *w, int value)
{
    wt_json_value_prefix(w);
    fputs(value ? "true" : "false", w->out);
}

void wt_json_null(WT_JsonWriter *w)
{
    wt_json_value_prefix(w);
    fputs("null", w->out);
}

void wt_json_finish(WT_JsonWriter *w)
{
    fputc('\n', w->out);
}

/* ------------------------------------------------------------------ */
/* High-level emitters                                                 */
/* ------------------------------------------------------------------ */

static void wt_json_emit_process(WT_JsonWriter *w, const WT_ProcessInfo *p)
{
    wt_json_begin_object(w);
    wt_json_key(w, "pid");                wt_json_uint64(w, p->pid);
    wt_json_key(w, "name");               wt_json_wstring(w, p->name);
    wt_json_key(w, "working_set_bytes");  wt_json_uint64(w, p->working_set_bytes);
    wt_json_key(w, "private_bytes");      wt_json_uint64(w, p->private_bytes);
    wt_json_key(w, "read_bytes");         wt_json_uint64(w, p->read_bytes);
    wt_json_key(w, "write_bytes");        wt_json_uint64(w, p->write_bytes);
    wt_json_key(w, "cpu_percent");
    if (p->cpu_percent < 0.0) {
        wt_json_null(w);
    } else {
        wt_json_double(w, p->cpu_percent);
    }
    wt_json_end_object(w);
}

static void wt_json_emit_envelope_head(WT_JsonWriter *w)
{
    char ts[32];
    if (wt_now_iso8601_utc(ts, sizeof(ts)) != WT_OK) {
        ts[0] = '\0';
    }
    wt_json_key(w, "version");       wt_json_string(w, WT_VERSION_STRING);
    wt_json_key(w, "timestamp_utc"); wt_json_string(w, ts);
}

static void wt_json_emit_recommendation(WT_JsonWriter *w,
                                        const WT_Recommendation *r)
{
    wt_json_begin_object(w);
    wt_json_key(w, "id");                 wt_json_string(w, r->id);
    wt_json_key(w, "title");              wt_json_string(w, r->title);
    wt_json_key(w, "severity");           wt_json_string(w, wt_severity_to_string(r->severity));
    wt_json_key(w, "risk");               wt_json_string(w, wt_risk_to_string(r->risk));
    wt_json_key(w, "reason");             wt_json_string(w, r->reason);
    wt_json_key(w, "action");             wt_json_string(w, r->action);
    wt_json_key(w, "requires_admin");     wt_json_bool(w, r->requires_admin);
    wt_json_key(w, "rollback_available"); wt_json_bool(w, r->rollback_available);
    wt_json_key(w, "confidence_percent"); wt_json_uint64(w, (unsigned long long)r->confidence_percent);
    wt_json_end_object(w);
}

static void wt_json_emit_recommendations_array(WT_JsonWriter *w,
                                               const WT_RecommendationList *recs)
{
    wt_json_key(w, "recommendations");
    wt_json_begin_array(w);
    if (recs != NULL) {
        for (size_t i = 0; i < recs->count; ++i) {
            wt_json_emit_recommendation(w, &recs->items[i]);
        }
    }
    wt_json_end_array(w);
}

void wt_print_scan_report_json(const WT_ScanReport *report,
                               const WT_RecommendationList *recs, FILE *out)
{
    WT_JsonWriter w;
    wt_json_init(&w, out);

    wt_json_begin_object(&w);
    wt_json_emit_envelope_head(&w);

    /* system */
    wt_json_key(&w, "system");
    wt_json_begin_object(&w);
    wt_json_key(&w, "available"); wt_json_bool(&w, report->os_ok);
    if (report->os_ok) {
        wt_json_key(&w, "os");        wt_json_wstring(&w, report->os.product_name);
        wt_json_key(&w, "arch");      wt_json_wstring(&w, report->os.arch);
        wt_json_key(&w, "hostname");  wt_json_wstring(&w, report->os.hostname);
        wt_json_key(&w, "uptime_ms"); wt_json_uint64(&w, report->os.uptime_ms);
    }
    wt_json_end_object(&w);

    /* cpu */
    wt_json_key(&w, "cpu");
    wt_json_begin_object(&w);
    wt_json_key(&w, "available");
    wt_json_bool(&w, report->cpu_ok && report->cpu.available);
    wt_json_key(&w, "logical_processors");
    wt_json_uint64(&w, report->cpu.logical_processor_count);
    wt_json_key(&w, "total_usage_percent");
    if (report->cpu_ok && report->cpu.available) {
        wt_json_double(&w, report->cpu.total_usage_percent);
    } else {
        wt_json_null(&w);
    }
    wt_json_end_object(&w);

    /* memory */
    wt_json_key(&w, "memory");
    wt_json_begin_object(&w);
    wt_json_key(&w, "available"); wt_json_bool(&w, report->memory_ok);
    if (report->memory_ok) {
        wt_json_key(&w, "total_bytes");     wt_json_uint64(&w, report->memory.total_physical_bytes);
        wt_json_key(&w, "available_bytes"); wt_json_uint64(&w, report->memory.available_physical_bytes);
        wt_json_key(&w, "used_bytes");      wt_json_uint64(&w, report->memory.used_physical_bytes);
        wt_json_key(&w, "used_percent");    wt_json_double(&w, report->memory.used_percent);
    }
    wt_json_end_object(&w);

    /* disk */
    wt_json_key(&w, "disk");
    wt_json_begin_object(&w);
    wt_json_key(&w, "available"); wt_json_bool(&w, report->disk_ok);
    wt_json_key(&w, "volumes");
    wt_json_begin_array(&w);
    if (report->disk_ok) {
        for (size_t i = 0; i < report->volume_count; ++i) {
            const WT_DiskVolumeMetrics *v = &report->volumes[i];
            wt_json_begin_object(&w);
            wt_json_key(&w, "root");         wt_json_wstring(&w, v->root_path);
            wt_json_key(&w, "total_bytes");  wt_json_uint64(&w, v->total_bytes);
            wt_json_key(&w, "free_bytes");   wt_json_uint64(&w, v->free_bytes);
            wt_json_key(&w, "free_percent"); wt_json_double(&w, v->free_percent);
            wt_json_end_object(&w);
        }
    }
    wt_json_end_array(&w);
    wt_json_key(&w, "active_available"); wt_json_bool(&w, report->disk_active_ok);
    wt_json_key(&w, "active_percent");
    if (report->disk_active_ok) {
        wt_json_double(&w, report->disk_active_percent);
    } else {
        wt_json_null(&w);
    }
    wt_json_end_object(&w);

    /* power */
    wt_json_key(&w, "power");
    wt_json_begin_object(&w);
    wt_json_key(&w, "available"); wt_json_bool(&w, report->power_ok);
    if (report->power_ok) {
        wt_json_key(&w, "plan");
        wt_json_string(&w, wt_power_scheme_name(report->power.scheme));
        wt_json_key(&w, "plan_name"); wt_json_wstring(&w, report->power.active_name);
        wt_json_key(&w, "on_ac");
        if (report->power.on_ac < 0) {
            wt_json_null(&w);
        } else {
            wt_json_bool(&w, report->power.on_ac);
        }
        wt_json_key(&w, "battery_percent");
        if (report->power.battery_percent < 0) {
            wt_json_null(&w);
        } else {
            wt_json_uint64(&w, (unsigned long long)report->power.battery_percent);
        }
    }
    wt_json_end_object(&w);

    /* processes */
    wt_json_key(&w, "processes");
    wt_json_begin_object(&w);
    wt_json_key(&w, "available"); wt_json_bool(&w, report->processes_ok);
    wt_json_key(&w, "top");
    wt_json_begin_array(&w);
    if (report->processes_ok) {
        for (size_t i = 0; i < report->top_process_count; ++i) {
            wt_json_emit_process(&w, &report->top_processes[i]);
        }
    }
    wt_json_end_array(&w);
    wt_json_end_object(&w);

    /* recommendations (populated in Phase 4) */
    wt_json_key(&w, "recommendations");
    wt_json_begin_array(&w);
    wt_json_end_array(&w);

    wt_json_end_object(&w);
    wt_json_finish(&w);
}

void wt_print_processes_json(const WT_ProcessInfo *items, size_t count, FILE *out)
{
    WT_JsonWriter w;
    wt_json_init(&w, out);

    wt_json_begin_object(&w);
    wt_json_emit_envelope_head(&w);
    wt_json_key(&w, "processes");
    wt_json_begin_array(&w);
    for (size_t i = 0; i < count; ++i) {
        wt_json_emit_process(&w, &items[i]);
    }
    wt_json_end_array(&w);
    wt_json_end_object(&w);
    wt_json_finish(&w);
}
