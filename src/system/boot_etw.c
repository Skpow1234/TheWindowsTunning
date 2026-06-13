#include "system/boot.h"

#include "common/log.h"
#include "platform/paths.h"
#include "platform/time.h"

#define INITGUID
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <strsafe.h>
#include <stdlib.h>

#pragma comment(lib, "advapi32.lib")

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
