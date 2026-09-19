#include "system/reliability.h"

#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winevt.h>
#include <strsafe.h>

#pragma comment(lib, "wevtapi.lib")

static const wchar_t *g_system_query =
    L"<QueryList>"
    L"  <Query Id='0' Path='System'>"
    L"    <Select>*[System[(EventID=41 or EventID=6008 or EventID=1001) and "
    L"      TimeCreated[timediff(@SystemTime) &lt;= 1209600000]]]</Select>"
    L"  </Query>"
    L"</QueryList>";

static const wchar_t *g_app_query =
    L"<QueryList>"
    L"  <Query Id='0' Path='Application'>"
    L"    <Select>*[System[(EventID=1000 or EventID=1001 or EventID=1002) and "
    L"      TimeCreated[timediff(@SystemTime) &lt;= 1209600000]]]</Select>"
    L"  </Query>"
    L"</QueryList>";

const char *wt_reliability_kind_name(WT_ReliabilityKind kind)
{
    switch (kind) {
    case WT_REL_KERNEL_POWER:        return "kernel_power";
    case WT_REL_UNEXPECTED_SHUTDOWN: return "unexpected_shutdown";
    case WT_REL_BUGCHECK:            return "bugcheck";
    case WT_REL_APP_CRASH:           return "app_crash";
    case WT_REL_APP_HANG:            return "app_hang";
    case WT_REL_WER_REPORT:          return "wer_report";
    default:                         return "unknown";
    }
}

void wt_reliability_init(WT_ReliabilityReport *report)
{
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
    report->lookback_days = WT_RELIABILITY_LOOKBACK_DAYS;
}

static int wt_rel_xml_get_data(const wchar_t *xml, const wchar_t *field,
                               wchar_t *out, size_t out_count)
{
    wchar_t open[96];
    const wchar_t *start = NULL;

    if (xml == NULL || field == NULL || out == NULL || out_count == 0) {
        return 0;
    }
    out[0] = L'\0';
    if (SUCCEEDED(StringCchPrintfW(open, ARRAYSIZE(open), L"Name='%ls'>",
                                   field))) {
        start = wcsstr(xml, open);
    }
    if (start == NULL &&
        SUCCEEDED(StringCchPrintfW(open, ARRAYSIZE(open), L"Name=\"%ls\">",
                                   field))) {
        start = wcsstr(xml, open);
    }
    if (start == NULL) {
        return 0;
    }
    start += wcslen(open);
    {
        const wchar_t *end = wcsstr(start, L"</Data>");
        size_t len;
        if (end == NULL) {
            return 0;
        }
        len = (size_t)(end - start);
        if (len >= out_count) {
            len = out_count - 1;
        }
        wmemcpy(out, start, len);
        out[len] = L'\0';
    }
    return 1;
}

static unsigned wt_rel_event_id(const wchar_t *xml)
{
    const wchar_t *marker = L"<EventID>";
    const wchar_t *start;
    if (xml == NULL) {
        return 0;
    }
    start = wcsstr(xml, marker);
    if (start == NULL) {
        return 0;
    }
    start += wcslen(marker);
    return (unsigned)wcstoul(start, NULL, 10);
}

static int wt_rel_time_utc(const wchar_t *xml, char *out, size_t out_count)
{
    const wchar_t *p;
    SYSTEMTIME st;
    FILETIME ft;
    wchar_t quote;

    if (out == NULL || out_count == 0) {
        return 0;
    }
    out[0] = '\0';
    if (xml == NULL) {
        return 0;
    }

    p = wcsstr(xml, L"SystemTime='");
    quote = L'\'';
    if (p == NULL) {
        p = wcsstr(xml, L"SystemTime=\"");
        quote = L'"';
    }
    if (p == NULL) {
        return 0;
    }
    p = wcschr(p, L'=');
    if (p == NULL) {
        return 0;
    }
    p += 2; /* skip =' or =" */

    memset(&st, 0, sizeof(st));
    if (swscanf_s(p, L"%hu-%hu-%huT%hu:%hu:%hu",
                  &st.wYear, &st.wMonth, &st.wDay,
                  &st.wHour, &st.wMinute, &st.wSecond) < 6) {
        return 0;
    }
    if (!SystemTimeToFileTime(&st, &ft)) {
        return 0;
    }
    if (snprintf(out, out_count, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                 (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                 (unsigned)st.wHour, (unsigned)st.wMinute,
                 (unsigned)st.wSecond) < 0) {
        return 0;
    }
    (void)quote;
    return 1;
}

static WT_Result wt_rel_render_xml(EVT_HANDLE event, wchar_t **out_xml)
{
    DWORD buffer_used = 0;
    DWORD property_count = 0;
    wchar_t *xml;

    if (!EvtRender(NULL, event, EvtRenderEventXml, 0, NULL, &buffer_used,
                   &property_count) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return WT_ERR_WIN32;
    }
    xml = (wchar_t *)malloc(buffer_used);
    if (xml == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    if (!EvtRender(NULL, event, EvtRenderEventXml, buffer_used, xml,
                   &buffer_used, &property_count)) {
        free(xml);
        return WT_ERR_WIN32;
    }
    *out_xml = xml;
    return WT_OK;
}

static void wt_rel_push_recent(WT_ReliabilityReport *r, WT_ReliabilityKind kind,
                               unsigned event_id, const char *time_utc,
                               const wchar_t *detail)
{
    WT_ReliabilityEvent *e;
    if (r->recent_count >= WT_MAX_RELIABILITY_EVENTS) {
        return;
    }
    e = &r->recent[r->recent_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->event_id = event_id;
    if (time_utc != NULL) {
        StringCchCopyA(e->time_utc, sizeof(e->time_utc), time_utc);
    }
    if (detail != NULL) {
        StringCchCopyW(e->detail, ARRAYSIZE(e->detail), detail);
    }
}

static void wt_rel_bump_crash_app(WT_ReliabilityReport *r, const wchar_t *name)
{
    size_t i;
    wchar_t base[96];
    const wchar_t *slash;

    if (name == NULL || name[0] == L'\0') {
        return;
    }
    slash = wcsrchr(name, L'\\');
    StringCchCopyW(base, ARRAYSIZE(base), slash != NULL ? slash + 1 : name);

    for (i = 0; i < r->crash_app_count; ++i) {
        if (_wcsicmp(r->crash_apps[i].name, base) == 0) {
            r->crash_apps[i].count++;
            return;
        }
    }
    if (r->crash_app_count >= WT_MAX_CRASH_APPS) {
        return;
    }
    StringCchCopyW(r->crash_apps[r->crash_app_count].name,
                   ARRAYSIZE(r->crash_apps[0].name), base);
    r->crash_apps[r->crash_app_count].count = 1;
    r->crash_app_count++;
}

static int wt_rel_crash_app_cmp(const void *a, const void *b)
{
    const WT_ReliabilityCrashApp *ca = (const WT_ReliabilityCrashApp *)a;
    const WT_ReliabilityCrashApp *cb = (const WT_ReliabilityCrashApp *)b;
    if (ca->count != cb->count) {
        return (cb->count > ca->count) ? 1 : -1;
    }
    return _wcsicmp(ca->name, cb->name);
}

static void wt_rel_handle_system_event(WT_ReliabilityReport *r,
                                       unsigned event_id, const wchar_t *xml)
{
    char ts[32];
    wchar_t detail[128];

    ts[0] = '\0';
    detail[0] = L'\0';
    (void)wt_rel_time_utc(xml, ts, sizeof(ts));

    if (event_id == 41) {
        r->kernel_power_count++;
        if (r->last_unexpected_utc[0] == '\0' && ts[0] != '\0') {
            StringCchCopyA(r->last_unexpected_utc, sizeof(r->last_unexpected_utc),
                           ts);
        }
        StringCchCopyW(detail, ARRAYSIZE(detail), L"Kernel-Power unexpected reboot");
        wt_rel_push_recent(r, WT_REL_KERNEL_POWER, event_id, ts, detail);
    } else if (event_id == 6008) {
        r->unexpected_shutdown_count++;
        if (r->last_unexpected_utc[0] == '\0' && ts[0] != '\0') {
            StringCchCopyA(r->last_unexpected_utc, sizeof(r->last_unexpected_utc),
                           ts);
        }
        StringCchCopyW(detail, ARRAYSIZE(detail), L"Previous shutdown was unexpected");
        wt_rel_push_recent(r, WT_REL_UNEXPECTED_SHUTDOWN, event_id, ts, detail);
    } else if (event_id == 1001) {
        /* Bugcheck / WER system error reporting */
        r->bugcheck_count++;
        if (!wt_rel_xml_get_data(xml, L"BugcheckCode", detail, ARRAYSIZE(detail))) {
            if (!wt_rel_xml_get_data(xml, L"param1", detail, ARRAYSIZE(detail))) {
                StringCchCopyW(detail, ARRAYSIZE(detail), L"Bugcheck / WER report");
            }
        }
        if (r->last_bugcheck_utc[0] == '\0' && ts[0] != '\0') {
            StringCchCopyA(r->last_bugcheck_utc, sizeof(r->last_bugcheck_utc), ts);
            StringCchCopyW(r->last_bugcheck_detail,
                           ARRAYSIZE(r->last_bugcheck_detail), detail);
        }
        wt_rel_push_recent(r, WT_REL_BUGCHECK, event_id, ts, detail);
    }
}

static void wt_rel_handle_app_event(WT_ReliabilityReport *r, unsigned event_id,
                                    const wchar_t *xml)
{
    char ts[32];
    wchar_t app[128];

    ts[0] = '\0';
    app[0] = L'\0';
    (void)wt_rel_time_utc(xml, ts, sizeof(ts));

    if (!wt_rel_xml_get_data(xml, L"AppName", app, ARRAYSIZE(app)) &&
        !wt_rel_xml_get_data(xml, L"Application", app, ARRAYSIZE(app)) &&
        !wt_rel_xml_get_data(xml, L"FaultingApplicationName", app,
                             ARRAYSIZE(app))) {
        /* Application Error often uses unnamed sequential Data — skip name. */
        StringCchCopyW(app, ARRAYSIZE(app), L"(unknown)");
    }

    if (event_id == 1000) {
        r->app_crash_count++;
        wt_rel_bump_crash_app(r, app);
        wt_rel_push_recent(r, WT_REL_APP_CRASH, event_id, ts, app);
    } else if (event_id == 1002) {
        r->app_hang_count++;
        wt_rel_bump_crash_app(r, app);
        wt_rel_push_recent(r, WT_REL_APP_HANG, event_id, ts, app);
    } else if (event_id == 1001) {
        r->wer_report_count++;
        wt_rel_push_recent(r, WT_REL_WER_REPORT, event_id, ts, app);
    }
}

static WT_Result wt_rel_scan_channel(WT_ReliabilityReport *r,
                                     const wchar_t *channel,
                                     const wchar_t *query_xml, int is_system)
{
    EVT_HANDLE query;
    EVT_HANDLE events[16];
    DWORD returned = 0;
    unsigned scanned = 0;
    const unsigned max_scan = 80;

    query = EvtQuery(NULL, channel, query_xml,
                     EvtQueryChannelPath | EvtQueryReverseDirection);
    if (query == NULL) {
        DWORD err = GetLastError();
        WT_LOGW("reliability EvtQuery %ls failed (err=%lu)", channel, err);
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        if (err == ERROR_EVT_CHANNEL_NOT_FOUND || err == ERROR_FILE_NOT_FOUND) {
            return WT_ERR_NOT_FOUND;
        }
        return WT_ERR_WIN32;
    }

    while (scanned < max_scan &&
           EvtNext(query, ARRAYSIZE(events), events, INFINITE, 0, &returned)) {
        DWORD i;
        for (i = 0; i < returned && scanned < max_scan; ++i) {
            wchar_t *xml = NULL;
            unsigned event_id;
            scanned++;
            if (wt_rel_render_xml(events[i], &xml) != WT_OK) {
                EvtClose(events[i]);
                continue;
            }
            event_id = wt_rel_event_id(xml);
            if (is_system) {
                wt_rel_handle_system_event(r, event_id, xml);
            } else {
                wt_rel_handle_app_event(r, event_id, xml);
            }
            free(xml);
            EvtClose(events[i]);
        }
    }
    EvtClose(query);
    return WT_OK;
}

static void wt_rel_filetime_utc(const FILETIME *ft, char *out, size_t count)
{
    SYSTEMTIME st;
    out[0] = '\0';
    if (ft == NULL || !FileTimeToSystemTime(ft, &st)) {
        return;
    }
    snprintf(out, count, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
             (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
}

static void wt_rel_add_dump(WT_ReliabilityReport *r, const wchar_t *name,
                            const wchar_t *location, unsigned long long size,
                            const FILETIME *mtime, int is_dir)
{
    WT_ReliabilityDumpMeta *d;
    if (r->dump_meta_count >= WT_MAX_DUMP_META) {
        return;
    }
    d = &r->dumps[r->dump_meta_count++];
    memset(d, 0, sizeof(*d));
    StringCchCopyW(d->name, ARRAYSIZE(d->name), name != NULL ? name : L"");
    StringCchCopyW(d->location, ARRAYSIZE(d->location),
                   location != NULL ? location : L"");
    d->size_bytes = size;
    d->is_directory = is_dir;
    wt_rel_filetime_utc(mtime, d->modified_utc, sizeof(d->modified_utc));
}

static void wt_rel_list_glob(WT_ReliabilityReport *r, const wchar_t *dir,
                             const wchar_t *pattern, const wchar_t *location,
                             int dirs_only)
{
    wchar_t search[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    unsigned added = 0;

    if (FAILED(StringCchPrintfW(search, ARRAYSIZE(search), L"%ls\\%ls", dir,
                                pattern))) {
        return;
    }
    find = FindFirstFileW(search, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        ULARGE_INTEGER sz;
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == L'\0' ||
             (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0'))) {
            continue;
        }
        if (dirs_only) {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                continue;
            }
        } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        sz.LowPart = fd.nFileSizeLow;
        sz.HighPart = fd.nFileSizeHigh;
        wt_rel_add_dump(r, fd.cFileName, location, sz.QuadPart,
                        &fd.ftLastWriteTime,
                        (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        added++;
        if (added >= 8 || r->dump_meta_count >= WT_MAX_DUMP_META) {
            break;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void wt_rel_collect_dump_meta(WT_ReliabilityReport *r)
{
    wchar_t windir[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t progdata[MAX_PATH];
    DWORD n;

    r->dumps_ok = 1;

    n = GetSystemWindowsDirectoryW(windir, ARRAYSIZE(windir));
    if (n > 0 && n < ARRAYSIZE(windir)) {
        if (SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%ls\\Minidump",
                                       windir))) {
            wt_rel_list_glob(r, path, L"*.dmp", L"minidump", 0);
        }
        if (SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%ls\\MEMORY.DMP",
                                       windir))) {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
                ULARGE_INTEGER sz;
                sz.LowPart = fad.nFileSizeLow;
                sz.HighPart = fad.nFileSizeHigh;
                wt_rel_add_dump(r, L"MEMORY.DMP", L"memory.dmp", sz.QuadPart,
                                &fad.ftLastWriteTime, 0);
            }
        }
    }

    n = GetEnvironmentVariableW(L"ProgramData", progdata, ARRAYSIZE(progdata));
    if (n > 0 && n < ARRAYSIZE(progdata)) {
        if (SUCCEEDED(StringCchPrintfW(
                path, ARRAYSIZE(path),
                L"%ls\\Microsoft\\Windows\\WER\\ReportArchive", progdata))) {
            wt_rel_list_glob(r, path, L"*", L"wer_archive", 1);
        }
        if (SUCCEEDED(StringCchPrintfW(
                path, ARRAYSIZE(path),
                L"%ls\\Microsoft\\Windows\\WER\\ReportQueue", progdata))) {
            wt_rel_list_glob(r, path, L"*", L"wer_queue", 1);
        }
    }
}

WT_Result wt_collect_reliability(WT_ReliabilityReport *report)
{
    WT_Result rs;
    WT_Result ra;
    int any_ok = 0;

    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_reliability_init(report);

    rs = wt_rel_scan_channel(report, L"System", g_system_query, 1);
    ra = wt_rel_scan_channel(report, L"Application", g_app_query, 0);

    if (rs == WT_OK || ra == WT_OK) {
        report->events_ok = 1;
        any_ok = 1;
    }

    if (report->crash_app_count > 1) {
        qsort(report->crash_apps, report->crash_app_count,
              sizeof(report->crash_apps[0]), wt_rel_crash_app_cmp);
    }

    wt_rel_collect_dump_meta(report);
    if (report->dump_meta_count > 0) {
        any_ok = 1;
    }

    if (report->bugcheck_count > 0 || report->kernel_power_count > 0 ||
        report->unexpected_shutdown_count > 0) {
        StringCchCopyW(report->note, ARRAYSIZE(report->note),
                       L"Recent unexpected shutdowns or bugchecks can leave "
                       L"the system feeling slow until a clean reboot and "
                       L"repair of failing apps/drivers. WinTune does not "
                       L"read or upload dump contents.");
    } else if (report->app_crash_count >= 3) {
        StringCchCopyW(report->note, ARRAYSIZE(report->note),
                       L"Repeated application crashes can waste CPU/disk on "
                       L"recovery and WER. Update or close the noisy apps; "
                       L"WinTune never claims to repair corruption.");
    } else {
        StringCchCopyW(report->note, ARRAYSIZE(report->note),
                       L"Read-only reliability summary. No dump contents are "
                       L"opened or uploaded.");
    }

    if (!any_ok && rs == WT_ERR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    if (!any_ok && (rs != WT_OK && ra != WT_OK)) {
        return (rs != WT_OK) ? rs : ra;
    }
    return WT_OK;
}
