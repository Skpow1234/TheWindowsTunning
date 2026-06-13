#include "service/service.h"
#include "platform/paths.h"
#include "platform/service_ipc.h"
#include "system/privilege.h"

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>

static WT_Result wt_service_get_exe_path(wchar_t *out, size_t count)
{
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)count);
    if (n == 0 || n >= count) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static WT_Result wt_service_build_binpath(wchar_t *out, size_t count)
{
    wchar_t exe[MAX_PATH];
    WT_Result r = wt_service_get_exe_path(exe, ARRAYSIZE(exe));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"\"%s\" service run", exe))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static SC_HANDLE wt_service_open_scm(unsigned desired_access)
{
    return OpenSCManagerW(NULL, NULL, desired_access);
}

const char *wt_service_account_kind_name(WT_ServiceAccountKind kind)
{
    switch (kind) {
    case WT_SVC_ACCOUNT_LOCAL_SYSTEM:  return "LocalSystem";
    case WT_SVC_ACCOUNT_LOCAL_SERVICE: return "LocalService";
    case WT_SVC_ACCOUNT_VIRTUAL:       return "VirtualServiceAccount";
    case WT_SVC_ACCOUNT_CUSTOM:        return "Custom";
    default:                           return "unknown";
    }
}

static WT_ServiceAccountKind wt_service_classify_account(const wchar_t *name)
{
    if (name == NULL || name[0] == L'\0') {
        return WT_SVC_ACCOUNT_LOCAL_SYSTEM;
    }
    if (_wcsicmp(name, L"LocalSystem") == 0 ||
        _wcsicmp(name, L"NT AUTHORITY\\LocalSystem") == 0) {
        return WT_SVC_ACCOUNT_LOCAL_SYSTEM;
    }
    if (_wcsicmp(name, L"NT AUTHORITY\\LocalService") == 0) {
        return WT_SVC_ACCOUNT_LOCAL_SERVICE;
    }
    if (wcsstr(name, L"NT SERVICE\\") == name) {
        return WT_SVC_ACCOUNT_VIRTUAL;
    }
    return WT_SVC_ACCOUNT_CUSTOM;
}

static void wt_service_resolve_account(const WT_ServiceInstallOptions *opts,
                                       const wchar_t **out_name,
                                       const wchar_t **out_password)
{
    static wchar_t virtual_name[64];
    *out_password = NULL;

    switch (opts->account_kind) {
    case WT_SVC_ACCOUNT_LOCAL_SERVICE:
        *out_name = L"NT AUTHORITY\\LocalService";
        break;
    case WT_SVC_ACCOUNT_VIRTUAL:
        if (FAILED(StringCchPrintfW(virtual_name, ARRAYSIZE(virtual_name),
                                    L"NT SERVICE\\%s", WT_SERVICE_NAME))) {
            *out_name = L"NT SERVICE\\WinTune";
        } else {
            *out_name = virtual_name;
        }
        break;
    case WT_SVC_ACCOUNT_CUSTOM:
        *out_name = opts->custom_account;
        *out_password = opts->custom_password;
        break;
    case WT_SVC_ACCOUNT_LOCAL_SYSTEM:
    default:
        *out_name = NULL;
        break;
    }
}

WT_Result wt_service_query_state(WT_ServiceInstallState *state,
                                 int *pipe_reachable)
{
    WT_ServiceConfigInfo info;
    WT_Result r = wt_service_query_config(&info);
    if (r != WT_OK) {
        return r;
    }
    if (state != NULL) {
        *state = info.state;
    }
    if (pipe_reachable != NULL) {
        *pipe_reachable = info.pipe_reachable;
    }
    return WT_OK;
}

WT_Result wt_service_query_config(WT_ServiceConfigInfo *info)
{
    if (info == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    info->state = WT_SVC_INST_NOT_INSTALLED;
    info->account_kind = WT_SVC_ACCOUNT_LOCAL_SYSTEM;

    SC_HANDLE scm = wt_service_open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return WT_ERR_WIN32;
    }

    SC_HANDLE svc = OpenServiceW(
        scm, WT_SERVICE_NAME,
        SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            return WT_OK;
        }
        return WT_ERR_WIN32;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytes = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                             sizeof(ssp), &bytes)) {
        switch (ssp.dwCurrentState) {
        case SERVICE_RUNNING:
            info->state = WT_SVC_INST_RUNNING;
            break;
        case SERVICE_STOPPED:
            info->state = WT_SVC_INST_STOPPED;
            break;
        case SERVICE_START_PENDING:
            info->state = WT_SVC_INST_START_PENDING;
            break;
        case SERVICE_STOP_PENDING:
            info->state = WT_SVC_INST_STOP_PENDING;
            break;
        default:
            info->state = WT_SVC_INST_STOPPED;
            break;
        }
    }

    DWORD cfg_bytes = 0;
    QueryServiceConfigW(svc, NULL, 0, &cfg_bytes);
    if (cfg_bytes > 0) {
        QUERY_SERVICE_CONFIGW *cfg =
            (QUERY_SERVICE_CONFIGW *)HeapAlloc(GetProcessHeap(), 0, cfg_bytes);
        if (cfg != NULL) {
            if (QueryServiceConfigW(svc, cfg, cfg_bytes, &cfg_bytes)) {
                info->auto_start = (cfg->dwStartType == SERVICE_AUTO_START);
                if (cfg->lpBinaryPathName != NULL) {
                    StringCchCopyW(info->binary_path, ARRAYSIZE(info->binary_path),
                                    cfg->lpBinaryPathName);
                }
                if (cfg->lpServiceStartName != NULL &&
                    cfg->lpServiceStartName[0] != L'\0') {
                    StringCchCopyW(info->account_name,
                                   ARRAYSIZE(info->account_name),
                                   cfg->lpServiceStartName);
                } else {
                    StringCchCopyW(info->account_name,
                                   ARRAYSIZE(info->account_name), L"LocalSystem");
                }
                info->account_kind =
                    wt_service_classify_account(info->account_name);
            }
            HeapFree(GetProcessHeap(), 0, cfg);
        }
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (info->state == WT_SVC_INST_RUNNING &&
        WaitNamedPipeW(WT_IPC_PIPE_NAME, 200)) {
        info->pipe_reachable = 1;
    }

    return WT_OK;
}

WT_Result wt_service_install(const WT_ServiceInstallOptions *opts)
{
    if (!wt_is_process_elevated()) {
        return WT_ERR_ACCESS_DENIED;
    }

    WT_ServiceInstallOptions defaults = {0};
    if (opts == NULL) {
        opts = &defaults;
        defaults.account_kind = WT_SVC_ACCOUNT_LOCAL_SYSTEM;
    }

    if (opts->account_kind == WT_SVC_ACCOUNT_CUSTOM &&
        (opts->custom_account == NULL || opts->custom_account[0] == L'\0')) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t binpath[1024];
    WT_Result r = wt_service_build_binpath(binpath, ARRAYSIZE(binpath));
    if (r != WT_OK) {
        return r;
    }

    wchar_t progdir[MAX_PATH];
    r = wt_paths_program_data_dir(progdir, ARRAYSIZE(progdir));
    if (r == WT_OK) {
        (void)wt_paths_ensure_dir(progdir);
    }

    SC_HANDLE scm = wt_service_open_scm(SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        return WT_ERR_WIN32;
    }

    SC_HANDLE existing = OpenServiceW(scm, WT_SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (existing != NULL) {
        CloseServiceHandle(existing);
        CloseServiceHandle(scm);
        return WT_ERR_NOT_SUPPORTED;
    }

    DWORD start_type = opts->auto_start ? SERVICE_AUTO_START : SERVICE_DEMAND_START;
    const wchar_t *account = NULL;
    const wchar_t *password = NULL;
    wt_service_resolve_account(opts, &account, &password);

    SC_HANDLE svc = CreateServiceW(
        scm, WT_SERVICE_NAME, WT_SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, start_type,
        SERVICE_ERROR_NORMAL, binpath, NULL, NULL, NULL, account, password);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        return WT_ERR_WIN32;
    }

    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = (LPWSTR)WT_SERVICE_DESCRIPTION;
    (void)ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    if (opts->account_kind == WT_SVC_ACCOUNT_VIRTUAL) {
        SERVICE_SID_INFO sid_info;
        sid_info.dwServiceSidType = SERVICE_SID_TYPE_UNRESTRICTED;
        (void)ChangeServiceConfig2W(svc, SERVICE_CONFIG_SERVICE_SID_INFO,
                                    &sid_info);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return WT_OK;
}

WT_Result wt_service_uninstall(void)
{
    if (!wt_is_process_elevated()) {
        return WT_ERR_ACCESS_DENIED;
    }

    (void)wt_service_stop();

    SC_HANDLE scm = wt_service_open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return WT_ERR_WIN32;
    }

    SC_HANDLE svc = OpenServiceW(scm, WT_SERVICE_NAME, DELETE);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            return WT_ERR_NOT_FOUND;
        }
        return WT_ERR_WIN32;
    }

    if (!DeleteService(svc)) {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return WT_ERR_WIN32;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return WT_OK;
}

WT_Result wt_service_start(void)
{
    SC_HANDLE scm = wt_service_open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return WT_ERR_WIN32;
    }

    SC_HANDLE svc = OpenServiceW(scm, WT_SERVICE_NAME,
                                 SERVICE_START | SERVICE_QUERY_STATUS);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        return WT_ERR_NOT_FOUND;
    }

    if (!StartServiceW(svc, 0, NULL)) {
        DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            return WT_OK;
        }
        return WT_ERR_WIN32;
    }

    for (int i = 0; i < 50; ++i) {
        SERVICE_STATUS_PROCESS ssp;
        DWORD bytes = 0;
        if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                                 sizeof(ssp), &bytes) &&
            ssp.dwCurrentState == SERVICE_RUNNING) {
            break;
        }
        Sleep(100);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return WT_OK;
}

WT_Result wt_service_stop(void)
{
    SC_HANDLE scm = wt_service_open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return WT_ERR_WIN32;
    }

    SC_HANDLE svc = OpenServiceW(scm, WT_SERVICE_NAME,
                                 SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            return WT_ERR_NOT_FOUND;
        }
        return WT_ERR_WIN32;
    }

    SERVICE_STATUS status;
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            return WT_OK;
        }
        return WT_ERR_WIN32;
    }

    for (int i = 0; i < 50; ++i) {
        SERVICE_STATUS_PROCESS ssp;
        DWORD bytes = 0;
        if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                                 sizeof(ssp), &bytes) &&
            ssp.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(100);
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return WT_OK;
}
