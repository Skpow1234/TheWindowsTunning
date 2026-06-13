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

WT_Result wt_service_query_state(WT_ServiceInstallState *state,
                                 int *pipe_reachable)
{
    if (state == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *state = WT_SVC_INST_NOT_INSTALLED;
    if (pipe_reachable != NULL) {
        *pipe_reachable = 0;
    }

    SC_HANDLE scm = wt_service_open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return WT_ERR_WIN32;
    }

    SC_HANDLE svc = OpenServiceW(scm, WT_SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            return WT_OK;
        }
        return WT_ERR_WIN32;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                              sizeof(ssp), &bytes)) {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return WT_ERR_WIN32;
    }

    switch (ssp.dwCurrentState) {
    case SERVICE_RUNNING:
        *state = WT_SVC_INST_RUNNING;
        break;
    case SERVICE_STOPPED:
        *state = WT_SVC_INST_STOPPED;
        break;
    case SERVICE_START_PENDING:
        *state = WT_SVC_INST_START_PENDING;
        break;
    case SERVICE_STOP_PENDING:
        *state = WT_SVC_INST_STOP_PENDING;
        break;
    default:
        *state = WT_SVC_INST_STOPPED;
        break;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (pipe_reachable != NULL && *state == WT_SVC_INST_RUNNING) {
        if (WaitNamedPipeW(WT_IPC_PIPE_NAME, 200)) {
            *pipe_reachable = 1;
        }
    }

    return WT_OK;
}

WT_Result wt_service_install(void)
{
    if (!wt_is_process_elevated()) {
        return WT_ERR_ACCESS_DENIED;
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

    SC_HANDLE svc = CreateServiceW(
        scm, WT_SERVICE_NAME, WT_SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL, binpath, NULL, NULL, NULL, NULL, NULL);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        return WT_ERR_WIN32;
    }

    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = (LPWSTR)WT_SERVICE_DESCRIPTION;
    (void)ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

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
