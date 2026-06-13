#include "system/boot.h"

#include "common/log.h"
#include "platform/paths.h"

#include <winevt.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "wevtapi.lib")

#define WT_BOOT_PERF_CHANNEL \
    L"Microsoft-Windows-Diagnostics-Performance/Operational"

#define WT_BOOT_SLOW_MS       60000u
#define WT_BOOT_APP_SLOW_MS   3000u

static const wchar_t *g_boot_query =
    L"<QueryList>"
    L"  <Query Id='0'>"
    L"    <Select Path='Microsoft-Windows-Diagnostics-Performance/Operational'>"
    L"      *[System[Provider[@Name='Microsoft-Windows-Diagnostics-Performance'] "
    L"        and (EventID=100 or EventID=101 or EventID=103)]]"
    L"    </Select>"
    L"  </Query>"
    L"</QueryList>";

void wt_boot_report_init(WT_BootReport *report)
{
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

const char *wt_boot_component_kind_name(WT_BootComponentKind kind)
{
    switch (kind) {
    case WT_BOOT_COMP_SERVICE:     return "service";
    case WT_BOOT_COMP_DRIVER:      return "driver";
    case WT_BOOT_COMP_APPLICATION: return "application";
    case WT_BOOT_COMP_DEGRADATION: return "degradation";
    default:                       return "unknown";
    }
}

static int wt_boot_wcs_contains_i(const wchar_t *hay, const wchar_t *needle)
{
    if (hay == NULL || needle == NULL || needle[0] == L'\0') {
        return 0;
    }
    wchar_t *dup = _wcsdup(hay);
    wchar_t *ndup = _wcsdup(needle);
    if (dup == NULL || ndup == NULL) {
        free(dup);
        free(ndup);
        return 0;
    }
    _wcsupr_s(dup, wcslen(dup) + 1);
    _wcsupr_s(ndup, wcslen(ndup) + 1);
    int found = (wcsstr(dup, ndup) != NULL);
    free(dup);
    free(ndup);
    return found;
}

static int wt_evt_xml_get_wstring(const wchar_t *xml, const wchar_t *field,
                                  wchar_t *out, size_t out_count)
{
    if (xml == NULL || field == NULL || out == NULL || out_count == 0) {
        return 0;
    }

    wchar_t open[96];
    const wchar_t *start = NULL;

    if (SUCCEEDED(StringCchPrintfW(open, ARRAYSIZE(open),
                                   L"Name='%ls'>", field))) {
        start = wcsstr(xml, open);
    }
    if (start == NULL &&
        SUCCEEDED(StringCchPrintfW(open, ARRAYSIZE(open),
                                   L"Name=\"%ls\">", field))) {
        start = wcsstr(xml, open);
    }
    if (start == NULL) {
        return 0;
    }

    start += wcslen(open);
    const wchar_t *end = wcsstr(start, L"</Data>");
    if (end == NULL) {
        return 0;
    }

    size_t len = (size_t)(end - start);
    if (len >= out_count) {
        len = out_count - 1;
    }
    wmemcpy(out, start, len);
    out[len] = L'\0';
    return 1;
}

static unsigned long wt_evt_xml_get_ulong(const wchar_t *xml, const wchar_t *field)
{
    wchar_t buf[32];
    if (!wt_evt_xml_get_wstring(xml, field, buf, ARRAYSIZE(buf))) {
        return 0;
    }
    return wcstoul(buf, NULL, 10);
}

static WT_BootComponentKind wt_boot_kind_from_text(const wchar_t *text,
                                                   unsigned event_id)
{
    if (event_id == 103) {
        return WT_BOOT_COMP_APPLICATION;
    }
    if (text == NULL) {
        return WT_BOOT_COMP_DEGRADATION;
    }
    if (wt_boot_wcs_contains_i(text, L"driver")) {
        return WT_BOOT_COMP_DRIVER;
    }
    if (wt_boot_wcs_contains_i(text, L"service")) {
        return WT_BOOT_COMP_SERVICE;
    }
    if (wt_boot_wcs_contains_i(text, L"application") ||
        wt_boot_wcs_contains_i(text, L"startup")) {
        return WT_BOOT_COMP_APPLICATION;
    }
    return WT_BOOT_COMP_DEGRADATION;
}

static int wt_boot_is_disk_heavy_text(const wchar_t *text)
{
    if (text == NULL) {
        return 0;
    }
    return wt_boot_wcs_contains_i(text, L"disk") ||
           wt_boot_wcs_contains_i(text, L"storage") ||
           wt_boot_wcs_contains_i(text, L"io");
}

static WT_BootComponent *wt_boot_add_component(WT_BootReport *report)
{
    if (report->component_count >= WT_MAX_BOOT_COMPONENTS) {
        return NULL;
    }
    WT_BootComponent *c = &report->components[report->component_count++];
    memset(c, 0, sizeof(*c));
    return c;
}

static void wt_boot_apply_event100(const wchar_t *xml, WT_BootReport *report)
{
    unsigned long boot_time = wt_evt_xml_get_ulong(xml, L"BootTime");
    if (boot_time == 0) {
        boot_time = wt_evt_xml_get_ulong(xml, L"BootTimeMs");
    }
    if (boot_time > 0) {
        report->boot_duration_ms = boot_time;
    }

    unsigned long main_path = wt_evt_xml_get_ulong(xml, L"MainPathBootTime");
    if (main_path == 0) {
        main_path = wt_evt_xml_get_ulong(xml, L"BootMainPathLoadTime");
    }
    if (main_path > 0) {
        report->main_path_ms = main_path;
    }

    unsigned long kernel = wt_evt_xml_get_ulong(xml, L"BootKernelInitTime");
    if (kernel > 0) {
        report->kernel_init_ms = kernel;
    }

    unsigned long driver = wt_evt_xml_get_ulong(xml, L"BootDriverInitTime");
    if (driver > 0) {
        report->driver_init_ms = driver;
    }

    unsigned long post = wt_evt_xml_get_ulong(xml, L"BootPostBootTime");
    if (post > 0) {
        report->post_boot_ms = post;
    }

    unsigned long degraded = wt_evt_xml_get_ulong(xml, L"BootIsDegradation");
    report->is_degraded = (degraded != 0);
}

static void wt_boot_apply_degradation_event(unsigned event_id, const wchar_t *xml,
                                            WT_BootReport *report)
{
    wchar_t name[128] = {0};
    wchar_t detail[256] = {0};
    unsigned long duration = 0;

    if (event_id == 103) {
        (void)wt_evt_xml_get_wstring(xml, L"FriendlyName", name, ARRAYSIZE(name));
        duration = wt_evt_xml_get_ulong(xml, L"DegradationDuration");
        if (duration == 0) {
            duration = wt_evt_xml_get_ulong(xml, L"DegradationTime");
        }
        (void)wt_evt_xml_get_wstring(xml, L"DegradationReason", detail,
                                     ARRAYSIZE(detail));
    } else {
        (void)wt_evt_xml_get_wstring(xml, L"PathRoot", name, ARRAYSIZE(name));
        if (name[0] == L'\0') {
            (void)wt_evt_xml_get_wstring(xml, L"FriendlyName", name, ARRAYSIZE(name));
        }
        duration = wt_evt_xml_get_ulong(xml, L"DegradationDeltaMs");
        if (duration == 0) {
            duration = wt_evt_xml_get_ulong(xml, L"DegradationTime");
        }
        (void)wt_evt_xml_get_wstring(xml, L"DegradationReason", detail,
                                     ARRAYSIZE(detail));
    }

    if (name[0] == L'\0' && detail[0] == L'\0') {
        return;
    }

    WT_BootComponent *c = wt_boot_add_component(report);
    if (c == NULL) {
        return;
    }

    if (name[0] != L'\0') {
        StringCchCopyW(c->name, ARRAYSIZE(c->name), name);
    } else {
        StringCchCopyW(c->name, ARRAYSIZE(c->name), L"(unknown component)");
    }
    StringCchCopyW(c->detail, ARRAYSIZE(c->detail), detail);
    c->duration_ms = duration;
    c->kind = wt_boot_kind_from_text(detail[0] != L'\0' ? detail : name, event_id);
    c->is_disk_heavy = wt_boot_is_disk_heavy_text(detail);

    if (report->degradation_summary[0] == L'\0' && detail[0] != L'\0') {
        StringCchCopyW(report->degradation_summary, ARRAYSIZE(report->degradation_summary),
                       detail);
    }
}

static WT_Result wt_boot_render_event_xml(EVT_HANDLE event, wchar_t **out_xml)
{
    DWORD buffer_used = 0;
    DWORD property_count = 0;

    if (!EvtRender(NULL, event, EvtRenderEventXml, 0, NULL, &buffer_used,
                   &property_count) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return WT_ERR_WIN32;
    }

    wchar_t *xml = (wchar_t *)malloc(buffer_used);
    if (xml == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    if (!EvtRender(NULL, event, EvtRenderEventXml, buffer_used, xml, &buffer_used,
                   &property_count)) {
        free(xml);
        return WT_ERR_WIN32;
    }

    *out_xml = xml;
    return WT_OK;
}

static unsigned wt_boot_event_id_from_xml(const wchar_t *xml)
{
    const wchar_t *marker = L"<EventID>";
    const wchar_t *start = wcsstr(xml, marker);
    if (start == NULL) {
        return 0;
    }
    start += wcslen(marker);
    return (unsigned)wcstoul(start, NULL, 10);
}

WT_Result wt_collect_boot_from_event_log(WT_BootReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wt_boot_report_init(report);
    StringCchCopyW(report->source, ARRAYSIZE(report->source), L"event_log");

    EVT_HANDLE query = EvtQuery(NULL, WT_BOOT_PERF_CHANNEL, g_boot_query,
                                EvtQueryChannelPath | EvtQueryReverseDirection);
    if (query == NULL) {
        DWORD err = GetLastError();
        WT_LOGW("EvtQuery failed for boot channel (err=%lu)", err);
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        return WT_ERR_WIN32;
    }

    EVT_HANDLE events[32];
    DWORD returned = 0;
    int saw_event100 = 0;

    while (EvtNext(query, ARRAYSIZE(events), events, INFINITE, 0, &returned)) {
        for (DWORD i = 0; i < returned; ++i) {
            wchar_t *xml = NULL;
            if (wt_boot_render_event_xml(events[i], &xml) != WT_OK) {
                EvtClose(events[i]);
                continue;
            }

            unsigned event_id = wt_boot_event_id_from_xml(xml);
            if (event_id == 100 && !saw_event100) {
                wt_boot_apply_event100(xml, report);
                saw_event100 = 1;
            } else if (event_id == 101 || event_id == 103) {
                wt_boot_apply_degradation_event(event_id, xml, report);
            }

            free(xml);
            EvtClose(events[i]);
        }

        if (saw_event100 && report->component_count >= 16) {
            break;
        }
    }

    DWORD loop_err = GetLastError();
    EvtClose(query);

    if (!saw_event100) {
        if (loop_err == ERROR_NO_MORE_ITEMS) {
            return WT_ERR_NOT_FOUND;
        }
        return WT_ERR_WIN32;
    }

    if (report->is_degraded && report->degradation_summary[0] == L'\0') {
        StringCchCopyW(report->degradation_summary,
                       ARRAYSIZE(report->degradation_summary),
                       L"Boot degradation was reported by Windows.");
    }

    return WT_OK;
}

void wt_boot_apply_measured_startup(const WT_BootReport *boot,
                                    wchar_t *entry_name, wchar_t *entry_command,
                                    unsigned long *out_ms, int *out_matched)
{
    if (out_ms != NULL) {
        *out_ms = 0;
    }
    if (out_matched != NULL) {
        *out_matched = 0;
    }
    if (boot == NULL || boot->component_count == 0) {
        return;
    }

    for (size_t i = 0; i < boot->component_count; ++i) {
        const WT_BootComponent *c = &boot->components[i];
        if (c->kind != WT_BOOT_COMP_APPLICATION && c->kind != WT_BOOT_COMP_DEGRADATION) {
            continue;
        }
        if (c->duration_ms == 0) {
            continue;
        }

        int match = 0;
        if (entry_name != NULL && entry_name[0] != L'\0' &&
            wt_boot_wcs_contains_i(c->name, entry_name)) {
            match = 1;
        }
        if (!match && entry_command != NULL && entry_command[0] != L'\0' &&
            wt_boot_wcs_contains_i(entry_command, c->name)) {
            match = 1;
        }
        if (!match && entry_name != NULL && entry_name[0] != L'\0' &&
            wt_boot_wcs_contains_i(entry_name, c->name)) {
            match = 1;
        }

        if (match) {
            if (out_ms != NULL) {
                *out_ms = c->duration_ms;
            }
            if (out_matched != NULL) {
                *out_matched = 1;
            }
            return;
        }
    }
}

WT_Result wt_collect_boot_report(WT_BootReport *report,
                                 const wchar_t *etl_path_opt)
{
    WT_Result r = wt_collect_boot_from_event_log(report);
    if (r != WT_OK && r != WT_ERR_NOT_FOUND) {
        return r;
    }

    if (etl_path_opt != NULL && etl_path_opt[0] != L'\0') {
        WT_BootReport etl_part;
        wt_boot_report_init(&etl_part);
        WT_Result er = wt_boot_analyze_etl(etl_path_opt, &etl_part);
        if (er == WT_OK) {
            report->etl_event_count = etl_part.etl_event_count;
            StringCchCopyW(report->trace_path, ARRAYSIZE(report->trace_path),
                           etl_path_opt);
            if (report->source[0] != L'\0') {
                StringCchCopyW(report->source, ARRAYSIZE(report->source),
                               L"event_log+etl");
            } else {
                StringCchCopyW(report->source, ARRAYSIZE(report->source), L"etl");
            }
        }
    }

    if (report->boot_duration_ms == 0 && report->component_count == 0) {
        return WT_ERR_NOT_FOUND;
    }

    return WT_OK;
}
