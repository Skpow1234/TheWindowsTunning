#include "system/boot.h"

#include "common/log.h"
#include "platform/paths.h"
#include "platform/time.h"

#define INITGUID
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <rpc.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rpcrt4.lib")

/* {22FB2CD6-0E7B-422B-A0C7-2FAD1FD0E716} Microsoft-Windows-Kernel-Process */
static const GUID WT_ETW_KERNEL_PROCESS =
    {0x22fb2cd6, 0x0e7b, 0x422b,
     {0xa0, 0xc7, 0x2f, 0xad, 0x1f, 0xd0, 0xe7, 0x16}};

/* {CE1DBFB4-1371-4B69-9318-6EA24A48B30E} Microsoft-Windows-Diagnostics-Performance */
static const GUID WT_ETW_DIAG_PERF =
    {0xce1dbfb4, 0x1371, 0x4b69,
     {0x93, 0x18, 0x6e, 0xa2, 0x4a, 0x48, 0xb3, 0x0e}};

#define WT_BOOT_TRACE_LOGGER L"WinTuneLogin"
#define WT_BOOT_TRACE_DEFAULT_MS 60000u

typedef struct WT_EtlCountCtx {
    unsigned long long count;
} WT_EtlCountCtx;

static VOID WINAPI wt_boot_etl_callback(PEVENT_RECORD record)
{
    (void)record;
    WT_EtlCountCtx *ctx = (WT_EtlCountCtx *)record->UserContext;
    if (ctx != NULL) {
        ctx->count++;
    }
}

static WT_Result wt_paths_traces_dir(wchar_t *out, size_t count)
{
    wchar_t base[MAX_PATH];
    WT_Result r = wt_paths_user_data_dir(base, ARRAYSIZE(base));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\traces", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_boot_trace_login(unsigned duration_ms, wchar_t *etl_path,
                              size_t etl_path_count)
{
    if (etl_path == NULL || etl_path_count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (duration_ms == 0) {
        duration_ms = WT_BOOT_TRACE_DEFAULT_MS;
    }

    wchar_t traces_dir[MAX_PATH];
    WT_Result pr = wt_paths_traces_dir(traces_dir, ARRAYSIZE(traces_dir));
    if (pr != WT_OK) {
        return pr;
    }
    if (wt_paths_ensure_dir(traces_dir) != WT_OK) {
        return WT_ERR_WIN32;
    }

    char ts[32];
    if (wt_now_iso8601_utc(ts, sizeof(ts)) != WT_OK) {
        ts[0] = '\0';
    }

    wchar_t log_file[MAX_PATH];
    if (ts[0] != '\0') {
        if (FAILED(StringCchPrintfW(log_file, ARRAYSIZE(log_file),
                                  L"%s\\login-%hs.etl", traces_dir, ts))) {
            return WT_ERR_BUFFER_TOO_SMALL;
        }
        for (wchar_t *p = log_file; *p != L'\0'; ++p) {
            if (*p == L':' || *p == L'.') {
                *p = L'-';
            }
        }
    } else {
        if (FAILED(StringCchPrintfW(log_file, ARRAYSIZE(log_file),
                                  L"%s\\login-trace.etl", traces_dir))) {
            return WT_ERR_BUFFER_TOO_SMALL;
        }
    }

    const wchar_t *logger_name = WT_BOOT_TRACE_LOGGER;
    size_t logger_chars = wcslen(logger_name) + 1;
    size_t file_chars = wcslen(log_file) + 1;
    ULONG buffer_size = (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) +
                                logger_chars * sizeof(wchar_t) +
                                file_chars * sizeof(wchar_t));

    EVENT_TRACE_PROPERTIES *prop =
        (EVENT_TRACE_PROPERTIES *)calloc(1, buffer_size);
    if (prop == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    prop->Wnode.BufferSize = buffer_size;
    prop->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    prop->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    prop->MaximumFileSize = 64;
    prop->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    prop->LogFileNameOffset =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + logger_chars * sizeof(wchar_t));

    wchar_t *name_slot =
        (wchar_t *)((BYTE *)prop + prop->LoggerNameOffset);
    wcscpy_s(name_slot, logger_chars, logger_name);

    wchar_t *file_slot =
        (wchar_t *)((BYTE *)prop + prop->LogFileNameOffset);
    wcscpy_s(file_slot, file_chars, log_file);

    TRACEHANDLE session = 0;
    ULONG start_rc = StartTraceW(&session, logger_name, prop);
    if (start_rc != ERROR_SUCCESS) {
        WT_LOGW("StartTraceW failed (rc=%lu)", start_rc);
        free(prop);
        if (start_rc == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        return WT_ERR_WIN32;
    }

    ULONG enable_rc = EnableTraceEx2(session, &WT_ETW_KERNEL_PROCESS,
                                     EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                     TRACE_LEVEL_INFORMATION, 0, 0, 0, NULL);
    if (enable_rc != ERROR_SUCCESS) {
        WT_LOGW("EnableTraceEx2 Kernel-Process failed (rc=%lu)", enable_rc);
    }

    enable_rc = EnableTraceEx2(session, &WT_ETW_DIAG_PERF,
                               EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                               TRACE_LEVEL_INFORMATION, 0, 0, 0, NULL);
    if (enable_rc != ERROR_SUCCESS) {
        WT_LOGW("EnableTraceEx2 Diagnostics-Performance failed (rc=%lu)",
                enable_rc);
    }

    Sleep(duration_ms);

    ULONG stop_rc = ControlTraceW(session, logger_name, prop,
                                  EVENT_TRACE_CONTROL_STOP);
    free(prop);

    if (stop_rc != ERROR_SUCCESS) {
        WT_LOGW("ControlTraceW stop failed (rc=%lu)", stop_rc);
        return WT_ERR_WIN32;
    }

    StringCchCopyW(etl_path, etl_path_count, log_file);
    return WT_OK;
}

WT_Result wt_boot_analyze_etl(const wchar_t *etl_path, WT_BootReport *report)
{
    if (etl_path == NULL || report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wt_boot_report_init(report);
    StringCchCopyW(report->source, ARRAYSIZE(report->source), L"etl");
    StringCchCopyW(report->trace_path, ARRAYSIZE(report->trace_path), etl_path);

    EVENT_TRACE_LOGFILEW log = {0};
    log.LogFileName = (LPWSTR)etl_path;
    log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = wt_boot_etl_callback;

    WT_EtlCountCtx ctx = {0};
    log.Context = &ctx;

    TRACEHANDLE trace = OpenTraceW(&log);
    if (trace == INVALID_PROCESSTRACE_HANDLE) {
        return WT_ERR_WIN32;
    }

    ULONG rc = ProcessTrace(&trace, 1, NULL, NULL);
    CloseTrace(trace);

    if (rc != ERROR_SUCCESS && rc != ERROR_CANCELLED) {
        return WT_ERR_WIN32;
    }

    report->etl_event_count = (unsigned long)ctx.count;
    return WT_OK;
}

/* -------------------------------------------------------------------------- */
/* Phase 31 — reboot-spanning Autologger                                      */
/* -------------------------------------------------------------------------- */

#define WT_BOOT_AUTOLOGGER_NAME L"WinTuneBoot"
#define WT_BOOT_AUTOLOGGER_KEY \
    L"SYSTEM\\CurrentControlSet\\Control\\WMI\\Autologger\\WinTuneBoot"

typedef struct WT_BootArmStateFile {
    int armed;
    wchar_t etl_path[MAX_PATH];
    char armed_utc[40];
    unsigned long long boot_time_100ns; /* FILETIME of boot when armed */
} WT_BootArmStateFile;

static void wt_boot_current_boot_time_100ns(unsigned long long *out)
{
    FILETIME now;
    ULARGE_INTEGER u;

    GetSystemTimeAsFileTime(&now);
    u.LowPart = now.dwLowDateTime;
    u.HighPart = now.dwHighDateTime;
    u.QuadPart -= GetTickCount64() * 10000ULL;
    *out = u.QuadPart;
}

static WT_Result wt_boot_traces_machine_dir(wchar_t *out, size_t count)
{
    wchar_t base[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(base, ARRAYSIZE(base));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\traces", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static WT_Result wt_boot_arm_state_path(wchar_t *out, size_t count)
{
    wchar_t base[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(base, ARRAYSIZE(base));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\boot_arm.state", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static WT_Result wt_boot_write_arm_state(const WT_BootArmStateFile *st)
{
    wchar_t path[MAX_PATH];
    wchar_t dir[MAX_PATH];
    FILE *f = NULL;
    WT_Result r;

    if (st == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    if (wt_paths_ensure_dir(dir) != WT_OK) {
        return WT_ERR_WIN32;
    }
    r = wt_boot_arm_state_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }
    if (_wfopen_s(&f, path, L"wb") != 0 || f == NULL) {
        return WT_ERR_WIN32;
    }
    fprintf(f, "armed=%d\n", st->armed);
    fprintf(f, "boot_time_100ns=%llu\n",
            (unsigned long long)st->boot_time_100ns);
    fprintf(f, "armed_utc=%s\n", st->armed_utc[0] != '\0' ? st->armed_utc : "");
    fwprintf(f, L"etl=%ls\n", st->etl_path);
    fclose(f);
    return WT_OK;
}

static WT_Result wt_boot_read_arm_state(WT_BootArmStateFile *st)
{
    wchar_t path[MAX_PATH];
    FILE *f = NULL;
    char line[512];
    WT_Result r;

    if (st == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(st, 0, sizeof(*st));
    r = wt_boot_arm_state_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }
    if (_wfopen_s(&f, path, L"rb") != 0 || f == NULL) {
        return WT_ERR_NOT_FOUND;
    }
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (strncmp(line, "armed=", 6) == 0) {
            st->armed = atoi(line + 6);
        } else if (strncmp(line, "boot_time_100ns=", 16) == 0) {
            st->boot_time_100ns =
                (unsigned long long)_strtoui64(line + 16, NULL, 10);
        } else if (strncmp(line, "armed_utc=", 10) == 0) {
            char *v = line + 10;
            size_t n = strlen(v);
            while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r')) {
                v[--n] = '\0';
            }
            StringCchCopyA(st->armed_utc, sizeof(st->armed_utc), v);
        } else if (strncmp(line, "etl=", 4) == 0) {
            /* UTF-8 path written as narrow; we write wide with fwprintf so
             * re-read via wide helper below. */
        }
    }
    fclose(f);

    /* Re-open as wide to read etl= line reliably. */
    if (_wfopen_s(&f, path, L"rb") == 0 && f != NULL) {
        wchar_t wline[MAX_PATH + 16];
        while (fgetws(wline, (int)ARRAYSIZE(wline), f) != NULL) {
            if (wcsncmp(wline, L"etl=", 4) == 0) {
                wchar_t *p = wline + 4;
                size_t n = wcslen(p);
                while (n > 0 && (p[n - 1] == L'\n' || p[n - 1] == L'\r')) {
                    p[--n] = L'\0';
                }
                StringCchCopyW(st->etl_path, ARRAYSIZE(st->etl_path), p);
            }
        }
        fclose(f);
    }
    return WT_OK;
}

static int wt_boot_autologger_present(void)
{
    HKEY key = NULL;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, WT_BOOT_AUTOLOGGER_KEY, 0,
                            KEY_READ, &key);
    if (rc != ERROR_SUCCESS) {
        return 0;
    }
    RegCloseKey(key);
    return 1;
}

static int wt_boot_session_running(void)
{
    ULONG buffer_size =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + 64 * sizeof(wchar_t) +
                MAX_PATH * sizeof(wchar_t));
    EVENT_TRACE_PROPERTIES *prop =
        (EVENT_TRACE_PROPERTIES *)calloc(1, buffer_size);
    ULONG rc;

    if (prop == NULL) {
        return 0;
    }
    prop->Wnode.BufferSize = buffer_size;
    prop->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    prop->LogFileNameOffset =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + 64 * sizeof(wchar_t));
    rc = ControlTraceW(0, WT_BOOT_AUTOLOGGER_NAME, prop,
                       EVENT_TRACE_CONTROL_QUERY);
    free(prop);
    return rc == ERROR_SUCCESS;
}

static WT_Result wt_boot_set_dword(HKEY key, const wchar_t *name, DWORD value)
{
    LONG rc = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value,
                             sizeof(value));
    return (rc == ERROR_SUCCESS) ? WT_OK : WT_ERR_WIN32;
}

static WT_Result wt_boot_set_sz(HKEY key, const wchar_t *name,
                                const wchar_t *value)
{
    size_t bytes = (wcslen(value) + 1) * sizeof(wchar_t);
    LONG rc = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                             (DWORD)bytes);
    return (rc == ERROR_SUCCESS) ? WT_OK : WT_ERR_WIN32;
}

static WT_Result wt_boot_enable_provider(HKEY session, const GUID *guid)
{
    wchar_t sub[64];
    HKEY pkey = NULL;
    LONG rc;
    WT_Result r;

    StringCchPrintfW(sub, ARRAYSIZE(sub),
                     L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                     guid->Data1, guid->Data2, guid->Data3, guid->Data4[0],
                     guid->Data4[1], guid->Data4[2], guid->Data4[3],
                     guid->Data4[4], guid->Data4[5], guid->Data4[6],
                     guid->Data4[7]);

    rc = RegCreateKeyExW(session, sub, 0, NULL, 0, KEY_SET_VALUE, NULL, &pkey,
                         NULL);
    if (rc != ERROR_SUCCESS) {
        return (rc == ERROR_ACCESS_DENIED) ? WT_ERR_ACCESS_DENIED : WT_ERR_WIN32;
    }
    r = wt_boot_set_dword(pkey, L"Enabled", 1);
    if (r == WT_OK) {
        r = wt_boot_set_dword(pkey, L"EnableLevel", TRACE_LEVEL_INFORMATION);
    }
    RegCloseKey(pkey);
    return r;
}

WT_Result wt_boot_stop_armed_session(void)
{
    ULONG buffer_size =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + 64 * sizeof(wchar_t) +
                MAX_PATH * sizeof(wchar_t));
    EVENT_TRACE_PROPERTIES *prop =
        (EVENT_TRACE_PROPERTIES *)calloc(1, buffer_size);
    ULONG rc;

    if (prop == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    prop->Wnode.BufferSize = buffer_size;
    prop->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    prop->LogFileNameOffset =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + 64 * sizeof(wchar_t));
    rc = ControlTraceW(0, WT_BOOT_AUTOLOGGER_NAME, prop,
                       EVENT_TRACE_CONTROL_STOP);
    free(prop);
    if (rc == ERROR_SUCCESS || rc == ERROR_WMI_INSTANCE_NOT_FOUND) {
        return WT_OK;
    }
    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    /* Session not running / unknown — treat as stopped. */
    WT_LOGW("ControlTraceW stop WinTuneBoot (rc=%lu); treating as stopped", rc);
    return WT_OK;
}

WT_Result wt_boot_arm_next(void)
{
    wchar_t traces_dir[MAX_PATH];
    wchar_t etl_path[MAX_PATH];
    HKEY session = NULL;
    LONG rc;
    GUID session_guid;
    wchar_t guid_str[64];
    WT_BootArmStateFile st;
    WT_Result r;
    RPC_STATUS uuid_rc;

    r = wt_boot_traces_machine_dir(traces_dir, ARRAYSIZE(traces_dir));
    if (r != WT_OK) {
        return r;
    }
    if (wt_paths_ensure_dir(traces_dir) != WT_OK) {
        return WT_ERR_WIN32;
    }
    if (FAILED(StringCchPrintfW(etl_path, ARRAYSIZE(etl_path),
                                L"%s\\boot-next.etl", traces_dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    /* Replace prior armed ETL so analyze does not use a stale file. */
    DeleteFileW(etl_path);

    uuid_rc = UuidCreate(&session_guid);
    if (uuid_rc != RPC_S_OK && uuid_rc != RPC_S_UUID_LOCAL_ONLY) {
        return WT_ERR_WIN32;
    }
    StringCchPrintfW(guid_str, ARRAYSIZE(guid_str),
                     L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                     session_guid.Data1, session_guid.Data2, session_guid.Data3,
                     session_guid.Data4[0], session_guid.Data4[1],
                     session_guid.Data4[2], session_guid.Data4[3],
                     session_guid.Data4[4], session_guid.Data4[5],
                     session_guid.Data4[6], session_guid.Data4[7]);

    /* Remove previous Autologger tree if present. */
    (void)RegDeleteTreeW(HKEY_LOCAL_MACHINE, WT_BOOT_AUTOLOGGER_KEY);

    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, WT_BOOT_AUTOLOGGER_KEY, 0, NULL, 0,
                         KEY_SET_VALUE | KEY_CREATE_SUB_KEY, NULL, &session,
                         NULL);
    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    if (rc != ERROR_SUCCESS) {
        return WT_ERR_WIN32;
    }

    r = wt_boot_set_sz(session, L"GUID", guid_str);
    if (r == WT_OK) {
        r = wt_boot_set_dword(session, L"Start", 1);
    }
    if (r == WT_OK) {
        /* Sequential file mode (Autologger default bits applied by ETW). */
        r = wt_boot_set_dword(session, L"LogFileMode",
                              EVENT_TRACE_FILE_MODE_SEQUENTIAL);
    }
    if (r == WT_OK) {
        r = wt_boot_set_sz(session, L"FileName", etl_path);
    }
    if (r == WT_OK) {
        r = wt_boot_set_dword(session, L"BufferSize", 64);
    }
    if (r == WT_OK) {
        r = wt_boot_set_dword(session, L"MinimumBuffers", 4);
    }
    if (r == WT_OK) {
        r = wt_boot_set_dword(session, L"MaximumBuffers", 32);
    }
    if (r == WT_OK) {
        r = wt_boot_set_dword(session, L"MaximumFileSize", 128);
    }
    if (r == WT_OK) {
        r = wt_boot_set_dword(session, L"ClockType", 2); /* system time */
    }
    if (r == WT_OK) {
        r = wt_boot_enable_provider(session, &WT_ETW_DIAG_PERF);
    }
    if (r == WT_OK) {
        r = wt_boot_enable_provider(session, &WT_ETW_KERNEL_PROCESS);
    }
    RegCloseKey(session);

    if (r != WT_OK) {
        (void)RegDeleteTreeW(HKEY_LOCAL_MACHINE, WT_BOOT_AUTOLOGGER_KEY);
        return r;
    }

    memset(&st, 0, sizeof(st));
    st.armed = 1;
    StringCchCopyW(st.etl_path, ARRAYSIZE(st.etl_path), etl_path);
    wt_boot_current_boot_time_100ns(&st.boot_time_100ns);
    if (wt_now_iso8601_utc(st.armed_utc, sizeof(st.armed_utc)) != WT_OK) {
        st.armed_utc[0] = '\0';
    }
    r = wt_boot_write_arm_state(&st);
    if (r != WT_OK) {
        WT_LOGW("Autologger armed but state file write failed");
    }
    return WT_OK;
}

WT_Result wt_boot_disarm(int keep_etl)
{
    WT_BootArmStateFile st;
    WT_Result stop_r;
    LONG rc;

    stop_r = wt_boot_stop_armed_session();
    if (stop_r == WT_ERR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }

    rc = RegDeleteTreeW(HKEY_LOCAL_MACHINE, WT_BOOT_AUTOLOGGER_KEY);
    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    /* ERROR_FILE_NOT_FOUND is fine (already gone). */

    if (wt_boot_read_arm_state(&st) == WT_OK) {
        if (!keep_etl && st.etl_path[0] != L'\0') {
            DeleteFileW(st.etl_path);
        }
    }

    {
        wchar_t path[MAX_PATH];
        if (wt_boot_arm_state_path(path, ARRAYSIZE(path)) == WT_OK) {
            DeleteFileW(path);
        }
    }
    return WT_OK;
}

WT_Result wt_boot_arm_status(WT_BootArmStatus *out)
{
    WT_BootArmStateFile st;
    unsigned long long now_boot = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    int have_state;
    int have_auto;
    int running;
    int rebooted;

    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    StringCchCopyW(out->session_name, ARRAYSIZE(out->session_name),
                   WT_BOOT_AUTOLOGGER_NAME);

    have_state = (wt_boot_read_arm_state(&st) == WT_OK && st.armed);
    have_auto = wt_boot_autologger_present();
    running = wt_boot_session_running();

    if (have_state) {
        StringCchCopyW(out->etl_path, ARRAYSIZE(out->etl_path), st.etl_path);
        StringCchCopyA(out->armed_utc, sizeof(out->armed_utc), st.armed_utc);
    } else if (have_auto) {
        /* Registry present without state — still treat as armed. */
        wchar_t traces_dir[MAX_PATH];
        if (wt_boot_traces_machine_dir(traces_dir, ARRAYSIZE(traces_dir)) ==
            WT_OK) {
            StringCchPrintfW(out->etl_path, ARRAYSIZE(out->etl_path),
                             L"%s\\boot-next.etl", traces_dir);
        }
    }

    if (out->etl_path[0] != L'\0' &&
        GetFileAttributesExW(out->etl_path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER sz;
        sz.LowPart = fad.nFileSizeLow;
        sz.HighPart = fad.nFileSizeHigh;
        out->etl_exists = 1;
        out->etl_bytes = sz.QuadPart;
    }

    wt_boot_current_boot_time_100ns(&now_boot);
    rebooted = 0;
    if (have_state && st.boot_time_100ns != 0) {
        /* Tolerate a few seconds of clock skew. */
        unsigned long long delta = (now_boot > st.boot_time_100ns)
                                       ? (now_boot - st.boot_time_100ns)
                                       : (st.boot_time_100ns - now_boot);
        if (delta > 5ULL * 10000000ULL) {
            rebooted = 1;
        }
    } else if (have_auto && out->etl_exists && out->etl_bytes > 0) {
        rebooted = 1;
    }
    out->reboot_occurred = rebooted;

    if (!have_state && !have_auto) {
        out->state = WT_BOOT_ARM_IDLE;
        return WT_OK;
    }
    if (!rebooted) {
        out->state = WT_BOOT_ARM_PENDING_REBOOT;
        return WT_OK;
    }
    if (running) {
        out->state = WT_BOOT_ARM_CAPTURING;
        return WT_OK;
    }
    if (out->etl_exists && out->etl_bytes > 0) {
        out->state = WT_BOOT_ARM_READY;
        return WT_OK;
    }
    /* Rebooted but no usable ETL yet — still pending capture. */
    out->state = WT_BOOT_ARM_CAPTURING;
    return WT_OK;
}

WT_Result wt_boot_resolve_reboot_etl(wchar_t *out, size_t count)
{
    WT_BootArmStatus st;
    WT_Result r;

    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out[0] = L'\0';
    r = wt_boot_arm_status(&st);
    if (r != WT_OK) {
        return r;
    }
    if (st.state != WT_BOOT_ARM_READY && st.state != WT_BOOT_ARM_CAPTURING) {
        return WT_ERR_NOT_FOUND;
    }
    if (!st.etl_exists || st.etl_bytes == 0 || st.etl_path[0] == L'\0') {
        return WT_ERR_NOT_FOUND;
    }
    if (FAILED(StringCchCopyW(out, count, st.etl_path))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

const char *wt_boot_arm_state_name(WT_BootArmState state)
{
    switch (state) {
    case WT_BOOT_ARM_IDLE:
        return "idle";
    case WT_BOOT_ARM_PENDING_REBOOT:
        return "pending_reboot";
    case WT_BOOT_ARM_CAPTURING:
        return "capturing";
    case WT_BOOT_ARM_READY:
        return "ready";
    default:
        return "idle";
    }
}
