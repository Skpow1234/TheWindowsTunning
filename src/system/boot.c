#include "system/boot.h"

#include "common/log.h"
#include "platform/paths.h"
#include "system/services.h"

#include <winevt.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "wevtapi.lib")

#define WT_BOOT_PERF_CHANNEL \
    L"Microsoft-Windows-Diagnostics-Performance/Operational"

#define WT_BOOT_SLOW_MS       60000u
#define WT_BOOT_APP_SLOW_MS   3000u
/* Fast Startup / hybrid boots typically show tiny kernel init times. */
#define WT_BOOT_WARM_KERNEL_MS 300u

static const wchar_t *g_boot_query =
    L"<QueryList>"
    L"  <Query Id='0'>"
    L"    <Select Path='Microsoft-Windows-Diagnostics-Performance/Operational'>"
    L"      *[System[Provider[@Name='Microsoft-Windows-Diagnostics-Performance'] "
    L"        and (EventID>=100 and EventID<=110)]]"
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

const char *wt_boot_kind_name(WT_BootKind kind)
{
    switch (kind) {
    case WT_BOOT_KIND_COLD: return "cold";
    case WT_BOOT_KIND_WARM: return "warm";
    default:                return "unknown";
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

static WT_BootComponentKind wt_boot_kind_from_event(unsigned event_id)
{
    switch (event_id) {
    case 101:
        return WT_BOOT_COMP_APPLICATION;
    case 102:
    case 109:
        return WT_BOOT_COMP_DRIVER;
    case 103:
        return WT_BOOT_COMP_SERVICE;
    default:
        return WT_BOOT_COMP_DEGRADATION;
    }
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

/* Parse Event XML TimeCreated SystemTime into 100ns since 1601 (UTC). */
static int wt_boot_parse_system_time(const wchar_t *xml, unsigned long long *out)
{
    const wchar_t *p;
    SYSTEMTIME st;
    FILETIME ft;
    ULARGE_INTEGER u;
    int y, mo, d, h, mi, s;

    if (xml == NULL || out == NULL) {
        return 0;
    }
    p = wcsstr(xml, L"SystemTime='");
    if (p != NULL) {
        p += 12;
    } else {
        p = wcsstr(xml, L"SystemTime=\"");
        if (p == NULL) {
            return 0;
        }
        p += 12;
    }
    if (swscanf_s(p, L"%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return 0;
    }
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)s;
    if (!SystemTimeToFileTime(&st, &ft)) {
        return 0;
    }
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    *out = u.QuadPart;
    return 1;
}

static int wt_boot_parse_boot_start(const char *iso, unsigned long long *out)
{
    SYSTEMTIME st;
    FILETIME ft;
    ULARGE_INTEGER u;
    int y, mo, d, h, mi, s;

    if (iso == NULL || iso[0] == '\0' || out == NULL) {
        return 0;
    }
    if (sscanf_s(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return 0;
    }
    ZeroMemory(&st, sizeof(st));
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)s;
    if (!SystemTimeToFileTime(&st, &ft)) {
        return 0;
    }
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    *out = u.QuadPart;
    return 1;
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

    unsigned long kernel = 0;
    wchar_t kbuf[32];
    if (wt_evt_xml_get_wstring(xml, L"BootKernelInitTime", kbuf,
                               ARRAYSIZE(kbuf))) {
        kernel = wcstoul(kbuf, NULL, 10);
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

/* Classify cold vs warm/hybrid from Event 100 timing.
 * Fast Startup (hybrid) restores a hibernated kernel, so BootKernelInitTime
 * is typically tiny. Full power-off cold boots show larger kernel init. */
static WT_BootKind wt_boot_classify_kind(unsigned long kernel_init_ms,
                                         int is_reboot_after_install,
                                         int kernel_field_present)
{
    if (is_reboot_after_install) {
        return WT_BOOT_KIND_COLD;
    }
    if (!kernel_field_present) {
        return WT_BOOT_KIND_UNKNOWN;
    }
    if (kernel_init_ms <= WT_BOOT_WARM_KERNEL_MS) {
        return WT_BOOT_KIND_WARM;
    }
    return WT_BOOT_KIND_COLD;
}

static void wt_boot_fill_history_entry(const wchar_t *xml,
                                       WT_BootHistoryEntry *e)
{
    wchar_t start[64];
    unsigned long degraded;
    unsigned long after_install;
    int kernel_present = 0;

    memset(e, 0, sizeof(*e));

    e->boot_duration_ms = wt_evt_xml_get_ulong(xml, L"BootTime");
    if (e->boot_duration_ms == 0) {
        e->boot_duration_ms = wt_evt_xml_get_ulong(xml, L"BootTimeMs");
    }
    e->main_path_ms = wt_evt_xml_get_ulong(xml, L"MainPathBootTime");
    if (e->main_path_ms == 0) {
        e->main_path_ms = wt_evt_xml_get_ulong(xml, L"BootMainPathLoadTime");
    }
    e->post_boot_ms = wt_evt_xml_get_ulong(xml, L"BootPostBootTime");
    e->driver_init_ms = wt_evt_xml_get_ulong(xml, L"BootDriverInitTime");

    /* Kernel init of 0 is meaningful (warm); distinguish from missing field. */
    if (wt_evt_xml_get_wstring(xml, L"BootKernelInitTime", start,
                               ARRAYSIZE(start))) {
        kernel_present = 1;
        e->kernel_init_ms = wcstoul(start, NULL, 10);
    }

    degraded = wt_evt_xml_get_ulong(xml, L"BootIsDegradation");
    e->is_degraded = (degraded != 0);

    after_install = wt_evt_xml_get_ulong(xml, L"BootIsRebootAfterInstall");
    e->is_reboot_after_install = (after_install != 0);

    if (wt_evt_xml_get_wstring(xml, L"BootStartTime", start, ARRAYSIZE(start))) {
        /* Keep a compact UTC-ish prefix for display/JSON. */
        size_t n = wcslen(start);
        if (n >= sizeof(e->boot_start_utc)) {
            n = sizeof(e->boot_start_utc) - 1;
        }
        for (size_t i = 0; i < n; ++i) {
            e->boot_start_utc[i] = (char)start[i];
        }
        e->boot_start_utc[n] = '\0';
        /* Trim fractional seconds / timezone noise for readability. */
        for (char *p = e->boot_start_utc; *p; ++p) {
            if (*p == '.') {
                *p = '\0';
                break;
            }
        }
    }

    e->kind = wt_boot_classify_kind(e->kernel_init_ms, e->is_reboot_after_install,
                                    kernel_present);
}

static void wt_boot_finalize_history(WT_BootReport *report)
{
    WT_BootHistory *h = &report->history;
    unsigned long long sum = 0;
    unsigned long long sum_cold = 0;
    unsigned long long sum_warm = 0;

    h->cold_count = 0;
    h->warm_count = 0;
    h->unknown_count = 0;
    h->slow_count = 0;
    h->degraded_count = 0;
    h->avg_duration_ms = 0;
    h->avg_cold_ms = 0;
    h->avg_warm_ms = 0;

    for (size_t i = 0; i < h->count; ++i) {
        const WT_BootHistoryEntry *e = &h->entries[i];
        sum += e->boot_duration_ms;
        if (e->boot_duration_ms >= WT_BOOT_SLOW_MS) {
            h->slow_count++;
        }
        if (e->is_degraded) {
            h->degraded_count++;
        }
        if (e->kind == WT_BOOT_KIND_COLD) {
            h->cold_count++;
            sum_cold += e->boot_duration_ms;
        } else if (e->kind == WT_BOOT_KIND_WARM) {
            h->warm_count++;
            sum_warm += e->boot_duration_ms;
        } else {
            h->unknown_count++;
        }
    }

    if (h->count > 0) {
        h->avg_duration_ms = (unsigned long)(sum / h->count);
    }
    if (h->cold_count > 0) {
        h->avg_cold_ms = (unsigned long)(sum_cold / h->cold_count);
    }
    if (h->warm_count > 0) {
        h->avg_warm_ms = (unsigned long)(sum_warm / h->warm_count);
    }

    if (h->count > 0) {
        report->last_boot_kind = h->entries[0].kind;
        if (h->entries[0].boot_start_utc[0] != '\0') {
            StringCchCopyA(report->boot_start_utc, sizeof(report->boot_start_utc),
                           h->entries[0].boot_start_utc);
        }
    }
}

static void wt_boot_apply_degradation_event(unsigned event_id, const wchar_t *xml,
                                            WT_BootReport *report,
                                            unsigned long long boot_start_100ns)
{
    wchar_t name[128] = {0};
    wchar_t detail[256] = {0};
    unsigned long duration = 0;
    unsigned long long event_time = 0;

    if (event_id < 101 || event_id > 110) {
        return;
    }

    (void)wt_evt_xml_get_wstring(xml, L"FriendlyName", name, ARRAYSIZE(name));
    if (name[0] == L'\0') {
        (void)wt_evt_xml_get_wstring(xml, L"Name", name, ARRAYSIZE(name));
    }
    if (name[0] == L'\0') {
        (void)wt_evt_xml_get_wstring(xml, L"PathRoot", name, ARRAYSIZE(name));
    }
    if (name[0] == L'\0') {
        (void)wt_evt_xml_get_wstring(xml, L"FileName", name, ARRAYSIZE(name));
    }
    if (name[0] == L'\0') {
        (void)wt_evt_xml_get_wstring(xml, L"DeviceName", name, ARRAYSIZE(name));
    }

    duration = wt_evt_xml_get_ulong(xml, L"DegradationDuration");
    if (duration == 0) {
        duration = wt_evt_xml_get_ulong(xml, L"DegradationDeltaMs");
    }
    if (duration == 0) {
        duration = wt_evt_xml_get_ulong(xml, L"DegradationTime");
    }
    if (duration == 0) {
        duration = wt_evt_xml_get_ulong(xml, L"TotalTime");
    }

    (void)wt_evt_xml_get_wstring(xml, L"DegradationReason", detail,
                                 ARRAYSIZE(detail));

    if (name[0] == L'\0' && detail[0] == L'\0' && duration == 0) {
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
    c->event_id = event_id;
    c->kind = wt_boot_kind_from_event(event_id);
    c->is_disk_heavy = wt_boot_is_disk_heavy_text(detail);

    if (boot_start_100ns != 0 &&
        wt_boot_parse_system_time(xml, &event_time) &&
        event_time >= boot_start_100ns) {
        c->start_offset_ms =
            (unsigned long)((event_time - boot_start_100ns) / 10000ULL);
    }

    if (report->degradation_summary[0] == L'\0' && detail[0] != L'\0') {
        StringCchCopyW(report->degradation_summary,
                       ARRAYSIZE(report->degradation_summary), detail);
    }
}

static int wt_boot_comp_impact_cmp(const void *a, const void *b)
{
    const WT_BootComponent *ca = (const WT_BootComponent *)a;
    const WT_BootComponent *cb = (const WT_BootComponent *)b;
    if (ca->duration_ms > cb->duration_ms) {
        return -1;
    }
    if (ca->duration_ms < cb->duration_ms) {
        return 1;
    }
    /* Tie-break: earlier in boot first. */
    if (ca->start_offset_ms != 0 && cb->start_offset_ms != 0) {
        if (ca->start_offset_ms < cb->start_offset_ms) {
            return -1;
        }
        if (ca->start_offset_ms > cb->start_offset_ms) {
            return 1;
        }
    }
    return 0;
}

static void wt_boot_sort_waterfall(WT_BootReport *report)
{
    unsigned long long total = 0;
    if (report == NULL || report->component_count == 0) {
        return;
    }
    qsort(report->components, report->component_count, sizeof(WT_BootComponent),
          wt_boot_comp_impact_cmp);
    for (size_t i = 0; i < report->component_count; ++i) {
        total += report->components[i].duration_ms;
    }
    report->waterfall_total_ms = (unsigned long)total;
    report->waterfall_sorted = 1;
}

void wt_boot_correlate_services(WT_BootReport *report)
{
    WT_ServiceInfo *svcs = NULL;
    size_t svc_count = 0;

    if (report == NULL || report->component_count == 0) {
        return;
    }

    svcs = (WT_ServiceInfo *)calloc(WT_MAX_SERVICES, sizeof(WT_ServiceInfo));
    if (svcs == NULL) {
        return;
    }
    if (wt_collect_services(svcs, WT_MAX_SERVICES, &svc_count) != WT_OK ||
        svc_count == 0) {
        free(svcs);
        return;
    }

    for (size_t i = 0; i < report->component_count; ++i) {
        WT_BootComponent *c = &report->components[i];
        if (c->kind != WT_BOOT_COMP_SERVICE &&
            c->kind != WT_BOOT_COMP_APPLICATION &&
            c->kind != WT_BOOT_COMP_DRIVER) {
            continue;
        }
        for (size_t s = 0; s < svc_count; ++s) {
            const WT_ServiceInfo *svc = &svcs[s];
            int match = 0;
            if (svc->display_name[0] != L'\0' &&
                _wcsicmp(c->name, svc->display_name) == 0) {
                match = 1;
            } else if (svc->name[0] != L'\0' &&
                       _wcsicmp(c->name, svc->name) == 0) {
                match = 1;
            } else if (wcslen(c->name) >= 5 &&
                       ((svc->display_name[0] != L'\0' &&
                         wt_boot_wcs_contains_i(svc->display_name, c->name)) ||
                        (svc->name[0] != L'\0' &&
                         wt_boot_wcs_contains_i(c->name, svc->name)))) {
                match = 1;
            }
            if (!match) {
                continue;
            }
            c->service_matched = 1;
            StringCchCopyW(c->service_name, ARRAYSIZE(c->service_name),
                           svc->name);
            c->service_pid = svc->pid;
            StringCchCopyA(c->service_state, sizeof(c->service_state),
                           wt_service_state_name(svc->state));
            break;
        }
    }
    free(svcs);
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

    DWORD query_flags = EvtQueryChannelPath | EvtQueryReverseDirection;
    EVT_HANDLE query = EvtQuery(NULL, WT_BOOT_PERF_CHANNEL, g_boot_query, query_flags);

    if (query == NULL) {
        DWORD err = GetLastError();
        WT_LOGW("EvtQuery channel failed (err=%lu)", err);

        if (err == ERROR_ACCESS_DENIED) {
            wchar_t evtx[MAX_PATH];
            UINT n = GetSystemDirectoryW(evtx, ARRAYSIZE(evtx));
            if (n > 0 && n < ARRAYSIZE(evtx)) {
                if (SUCCEEDED(StringCchPrintfW(
                        evtx, ARRAYSIZE(evtx),
                        L"%s\\winevt\\Logs\\"
                        L"Microsoft-Windows-Diagnostics-Performance%%4Operational.evtx",
                        evtx))) {
                    query = EvtQuery(NULL, evtx, g_boot_query,
                                     EvtQueryFilePath | EvtQueryReverseDirection);
                    if (query != NULL) {
                        StringCchCopyW(report->source, ARRAYSIZE(report->source),
                                       L"event_log_file");
                    } else {
                        err = GetLastError();
                        WT_LOGW("EvtQuery evtx fallback failed (err=%lu)", err);
                    }
                }
            }
        }

        if (query == NULL) {
            if (err == ERROR_ACCESS_DENIED) {
                return WT_ERR_ACCESS_DENIED;
            }
            /* Common on fresh VMs / CI images where the Diagnostic-Performance
             * channel was never created. */
            if (err == ERROR_EVT_CHANNEL_NOT_FOUND ||
                err == ERROR_FILE_NOT_FOUND ||
                err == ERROR_PATH_NOT_FOUND) {
                return WT_ERR_NOT_FOUND;
            }
            return WT_ERR_WIN32;
        }
    }

    EVT_HANDLE events[32];
    DWORD returned = 0;
    int saw_event100 = 0;
    unsigned long long boot_start_100ns = 0;

    while (EvtNext(query, ARRAYSIZE(events), events, INFINITE, 0, &returned)) {
        for (DWORD i = 0; i < returned; ++i) {
            wchar_t *xml = NULL;
            if (wt_boot_render_event_xml(events[i], &xml) != WT_OK) {
                EvtClose(events[i]);
                continue;
            }

            unsigned event_id = wt_boot_event_id_from_xml(xml);
            if (event_id == 100) {
                if (report->history.count < WT_MAX_BOOT_HISTORY) {
                    wt_boot_fill_history_entry(
                        xml, &report->history.entries[report->history.count]);
                    report->history.count++;
                }
                if (!saw_event100) {
                    wt_boot_apply_event100(xml, report);
                    saw_event100 = 1;
                    /* Prefer full BootStartTime from XML for offset math. */
                    {
                        wchar_t start[64];
                        if (wt_evt_xml_get_wstring(xml, L"BootStartTime", start,
                                                   ARRAYSIZE(start))) {
                            char narrow[64];
                            size_t n = wcslen(start);
                            if (n >= sizeof(narrow)) {
                                n = sizeof(narrow) - 1;
                            }
                            for (size_t k = 0; k < n; ++k) {
                                narrow[k] = (char)start[k];
                            }
                            narrow[n] = '\0';
                            for (char *p = narrow; *p; ++p) {
                                if (*p == '.') {
                                    *p = '\0';
                                    break;
                                }
                            }
                            (void)wt_boot_parse_boot_start(narrow,
                                                           &boot_start_100ns);
                        }
                    }
                }
            } else if (event_id >= 101 && event_id <= 110 &&
                       report->history.count <= 1) {
                /* Degradation / waterfall events for the latest boot only. */
                wt_boot_apply_degradation_event(event_id, xml, report,
                                                boot_start_100ns);
            }

            free(xml);
            EvtClose(events[i]);
        }

        if (report->history.count >= WT_MAX_BOOT_HISTORY) {
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

    wt_boot_finalize_history(report);
    if (boot_start_100ns == 0 && report->boot_start_utc[0] != '\0') {
        (void)wt_boot_parse_boot_start(report->boot_start_utc, &boot_start_100ns);
    }
    wt_boot_sort_waterfall(report);
    wt_boot_correlate_services(report);

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
    wchar_t auto_etl[MAX_PATH];
    const wchar_t *etl = etl_path_opt;

    WT_Result r = wt_collect_boot_from_event_log(report);
    if (r != WT_OK && r != WT_ERR_NOT_FOUND) {
        return r;
    }

    if (etl == NULL || etl[0] == L'\0') {
        if (wt_boot_resolve_reboot_etl(auto_etl, ARRAYSIZE(auto_etl)) == WT_OK) {
            etl = auto_etl;
            /* Stop Autologger capture so analyze does not leave it running. */
            (void)wt_boot_stop_armed_session();
        } else {
            etl = NULL;
        }
    }

    if (etl != NULL && etl[0] != L'\0') {
        WT_BootReport etl_part;
        wt_boot_report_init(&etl_part);
        WT_Result er = wt_boot_analyze_etl(etl, &etl_part);
        if (er == WT_OK) {
            report->etl_event_count = etl_part.etl_event_count;
            StringCchCopyW(report->trace_path, ARRAYSIZE(report->trace_path),
                           etl);
            if (report->source[0] != L'\0') {
                StringCchCopyW(report->source, ARRAYSIZE(report->source),
                               L"event_log+etl");
            } else {
                StringCchCopyW(report->source, ARRAYSIZE(report->source), L"etl");
            }
        }
    }

    if (report->boot_duration_ms == 0 && report->component_count == 0 &&
        report->etl_event_count == 0) {
        return WT_ERR_NOT_FOUND;
    }

    return WT_OK;
}
