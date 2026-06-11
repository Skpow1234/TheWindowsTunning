#include "system/services.h"

#include <windows.h>
#include <winsvc.h>
#include <strsafe.h>
#include <stdlib.h>

const char *wt_service_state_name(WT_ServiceState state)
{
    switch (state) {
    case WT_SVC_STATE_STOPPED:          return "stopped";
    case WT_SVC_STATE_START_PENDING:    return "start-pending";
    case WT_SVC_STATE_STOP_PENDING:     return "stop-pending";
    case WT_SVC_STATE_RUNNING:          return "running";
    case WT_SVC_STATE_CONTINUE_PENDING: return "continue-pending";
    case WT_SVC_STATE_PAUSE_PENDING:    return "pause-pending";
    case WT_SVC_STATE_PAUSED:           return "paused";
    default:                            return "unknown";
    }
}

const char *wt_service_start_type_name(WT_ServiceStartType type)
{
    switch (type) {
    case WT_SVC_START_BOOT:     return "boot";
    case WT_SVC_START_SYSTEM:   return "system";
    case WT_SVC_START_AUTO:     return "auto";
    case WT_SVC_START_DEMAND:   return "manual";
    case WT_SVC_START_DISABLED: return "disabled";
    default:                    return "unknown";
    }
}

static WT_ServiceState wt_map_state(DWORD s)
{
    switch (s) {
    case SERVICE_STOPPED:          return WT_SVC_STATE_STOPPED;
    case SERVICE_START_PENDING:    return WT_SVC_STATE_START_PENDING;
    case SERVICE_STOP_PENDING:     return WT_SVC_STATE_STOP_PENDING;
    case SERVICE_RUNNING:          return WT_SVC_STATE_RUNNING;
    case SERVICE_CONTINUE_PENDING: return WT_SVC_STATE_CONTINUE_PENDING;
    case SERVICE_PAUSE_PENDING:    return WT_SVC_STATE_PAUSE_PENDING;
    case SERVICE_PAUSED:           return WT_SVC_STATE_PAUSED;
    default:                       return WT_SVC_STATE_UNKNOWN;
    }
}

static WT_ServiceStartType wt_map_start_type(DWORD t)
{
    switch (t) {
    case SERVICE_BOOT_START:   return WT_SVC_START_BOOT;
    case SERVICE_SYSTEM_START: return WT_SVC_START_SYSTEM;
    case SERVICE_AUTO_START:   return WT_SVC_START_AUTO;
    case SERVICE_DEMAND_START: return WT_SVC_START_DEMAND;
    case SERVICE_DISABLED:     return WT_SVC_START_DISABLED;
    default:                   return WT_SVC_START_UNKNOWN;
    }
}

/* Reads a single service's configured start type. Failure (e.g. access denied)
 * is non-fatal and reported as "unknown". */
static WT_ServiceStartType wt_query_start_type(SC_HANDLE scm, const wchar_t *name)
{
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_CONFIG);
    if (svc == NULL) {
        return WT_SVC_START_UNKNOWN;
    }

    WT_ServiceStartType result = WT_SVC_START_UNKNOWN;
    DWORD needed = 0;
    QueryServiceConfigW(svc, NULL, 0, &needed);
    if (needed > 0) {
        QUERY_SERVICE_CONFIGW *cfg = (QUERY_SERVICE_CONFIGW *)malloc(needed);
        if (cfg != NULL) {
            if (QueryServiceConfigW(svc, cfg, needed, &needed)) {
                result = wt_map_start_type(cfg->dwStartType);
            }
            free(cfg);
        }
    }
    CloseServiceHandle(svc);
    return result;
}

WT_Result wt_collect_services(WT_ServiceInfo *out,
                              size_t capacity,
                              size_t *out_count)
{
    if (out == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (scm == NULL) {
        return WT_ERR_ACCESS_DENIED;
    }

    DWORD bytes_needed = 0;
    DWORD returned = 0;
    DWORD resume = 0;
    /* First call sizes the buffer for all matching services. */
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_STATE_ALL, NULL, 0, &bytes_needed, &returned,
                          &resume, NULL);
    if (bytes_needed == 0) {
        CloseServiceHandle(scm);
        return WT_OK; /* no services (unexpected, but not an error) */
    }

    BYTE *buffer = (BYTE *)malloc(bytes_needed);
    if (buffer == NULL) {
        CloseServiceHandle(scm);
        return WT_ERR_OUT_OF_MEMORY;
    }

    WT_Result result = WT_OK;
    if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                               SERVICE_STATE_ALL, buffer, bytes_needed,
                               &bytes_needed, &returned, &resume, NULL)) {
        result = WT_ERR_WIN32;
        goto cleanup;
    }

    ENUM_SERVICE_STATUS_PROCESSW *services =
        (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
    for (DWORD i = 0; i < returned && *out_count < capacity; ++i) {
        const ENUM_SERVICE_STATUS_PROCESSW *s = &services[i];
        WT_ServiceInfo *info = &out[*out_count];
        ZeroMemory(info, sizeof(*info));

        StringCchCopyW(info->name, ARRAYSIZE(info->name),
                       s->lpServiceName ? s->lpServiceName : L"");
        StringCchCopyW(info->display_name, ARRAYSIZE(info->display_name),
                       s->lpDisplayName ? s->lpDisplayName : L"");
        info->state = wt_map_state(s->ServiceStatusProcess.dwCurrentState);
        info->pid = s->ServiceStatusProcess.dwProcessId;
        info->start_type = wt_query_start_type(scm, info->name);

        (*out_count)++;
    }

cleanup:
    free(buffer);
    CloseServiceHandle(scm);
    return result;
}
