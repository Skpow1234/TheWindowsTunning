#include "system/updates.h"

#include <winsvc.h>
#include <wuapi.h>
#include <strsafe.h>

typedef struct WT_ComScope {
    int initialized;
} WT_ComScope;

static WT_Result wt_com_begin(WT_ComScope *scope)
{
    if (scope == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    scope->initialized = 0;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        return WT_OK;
    }
    if (FAILED(hr)) {
        return WT_ERR_WIN32;
    }
    scope->initialized = 1;
    return WT_OK;
}

static void wt_com_end(WT_ComScope *scope)
{
    if (scope != NULL && scope->initialized) {
        CoUninitialize();
        scope->initialized = 0;
    }
}

static int wt_reg_key_exists(HKEY root, const wchar_t *subkey)
{
    HKEY key = NULL;
    LONG rc = RegOpenKeyExW(root, subkey, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS) {
        return 0;
    }
    RegCloseKey(key);
    return 1;
}

static int wt_reg_read_filetime(HKEY root, const wchar_t *subkey,
                                const wchar_t *value_name, FILETIME *out)
{
    if (out == NULL) {
        return 0;
    }
    ZeroMemory(out, sizeof(*out));

    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return 0;
    }

    BYTE data[16];
    DWORD size = sizeof(data);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(key, value_name, NULL, &type, data, &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        return 0;
    }

    if (type == REG_BINARY && size >= sizeof(FILETIME)) {
        memcpy(out, data, sizeof(FILETIME));
        return (out->dwLowDateTime != 0 || out->dwHighDateTime != 0) ? 1 : 0;
    }
    if (type == REG_QWORD && size >= sizeof(ULONGLONG)) {
        ULONGLONG q = 0;
        memcpy(&q, data, sizeof(q));
        out->dwLowDateTime = (DWORD)(q & 0xFFFFFFFFu);
        out->dwHighDateTime = (DWORD)(q >> 32);
        return (q != 0) ? 1 : 0;
    }
    return 0;
}

static int wt_reg_has_pending_file_rename(void)
{
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return 0;
    }

    DWORD type = 0;
    DWORD size = 0;
    LONG rc = RegQueryValueExW(key, L"PendingFileRenameOperations", NULL, &type,
                               NULL, &size);
    RegCloseKey(key);
    return (rc == ERROR_SUCCESS && size > 0) ? 1 : 0;
}

static int wt_query_wu_service_running(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return -1;
    }

    SC_HANDLE svc = OpenServiceW(scm, L"wuauserv", SERVICE_QUERY_STATUS);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        return -1;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytes = 0;
    int running = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                             sizeof(ssp), &bytes)) {
        running = (ssp.dwCurrentState == SERVICE_RUNNING) ? 1 : 0;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return running;
}

void wt_update_status_init(WT_UpdateStatus *status)
{
    if (status != NULL) {
        ZeroMemory(status, sizeof(*status));
        status->wu_service_running = -1;
    }
}

WT_Result wt_format_filetime_iso8601_utc(const FILETIME *ft, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    if (ft == NULL || (ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0)) {
        return WT_ERR_NOT_FOUND;
    }

    SYSTEMTIME st_utc;
    if (!FileTimeToSystemTime(ft, &st_utc)) {
        return WT_ERR_WIN32;
    }

    if (FAILED(StringCchPrintfA(out, out_cap, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                                st_utc.wYear, st_utc.wMonth, st_utc.wDay,
                                st_utc.wHour, st_utc.wMinute, st_utc.wSecond))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static void wt_collect_reboot_flags(WT_UpdateStatus *status)
{
    status->reboot_wu = wt_reg_key_exists(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired");
    status->reboot_cbs = wt_reg_key_exists(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending");
    status->reboot_pending_file_rename = wt_reg_has_pending_file_rename();
    status->reboot_required =
        (status->reboot_wu || status->reboot_cbs ||
         status->reboot_pending_file_rename) ? 1 : 0;
}

static void wt_collect_last_times(WT_UpdateStatus *status)
{
    static const wchar_t *WU_RESULTS =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\Results";

    wchar_t subkey[512];
    if (SUCCEEDED(StringCchPrintfW(subkey, ARRAYSIZE(subkey),
                                   L"%s\\Detect", WU_RESULTS))) {
        status->last_check_available = wt_reg_read_filetime(
            HKEY_LOCAL_MACHINE, subkey, L"LastSuccessTime", &status->last_check_utc);
    }

    if (SUCCEEDED(StringCchPrintfW(subkey, ARRAYSIZE(subkey),
                                   L"%s\\Install", WU_RESULTS))) {
        status->last_install_available = wt_reg_read_filetime(
            HKEY_LOCAL_MACHINE, subkey, L"LastSuccessTime",
            &status->last_install_utc);
    }
}

static void wt_bstr_copy_w(wchar_t *out, size_t out_count, BSTR b)
{
    if (out == NULL || out_count == 0) {
        return;
    }
    out[0] = L'\0';
    if (b != NULL) {
        StringCchCopyW(out, out_count, b);
    }
}

static WT_Result wt_collect_last_install_title(WT_UpdateStatus *status)
{
    WT_ComScope com;
    WT_Result r = wt_com_begin(&com);
    if (r != WT_OK) {
        return r;
    }

    IUpdateSession *session = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUpdateSession, (void **)&session);
    if (FAILED(hr) || session == NULL) {
        wt_com_end(&com);
        return WT_ERR_WIN32;
    }

    IUpdateSearcher *searcher = NULL;
    hr = session->lpVtbl->CreateUpdateSearcher(session, &searcher);
    if (FAILED(hr) || searcher == NULL) {
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        return WT_ERR_WIN32;
    }

    LONG total = 0;
    hr = searcher->lpVtbl->GetTotalHistoryCount(searcher, &total);
    if (FAILED(hr) || total <= 0) {
        searcher->lpVtbl->Release(searcher);
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        return WT_ERR_NOT_FOUND;
    }

    IUpdateHistoryEntryCollection *history = NULL;
    hr = searcher->lpVtbl->QueryHistory(searcher, total - 1, 1, &history);
    if (FAILED(hr) || history == NULL) {
        searcher->lpVtbl->Release(searcher);
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        return WT_ERR_NOT_FOUND;
    }

    IUpdateHistoryEntry *entry = NULL;
    hr = history->lpVtbl->get_Item(history, 0, &entry);
    if (SUCCEEDED(hr) && entry != NULL) {
        OperationResultCode result = orcNotStarted;
        entry->lpVtbl->get_ResultCode(entry, &result);
        if (result == orcSucceeded || result == orcSucceededWithErrors) {
            BSTR title = NULL;
            if (SUCCEEDED(entry->lpVtbl->get_Title(entry, &title))) {
                wt_bstr_copy_w(status->last_install_title,
                               ARRAYSIZE(status->last_install_title), title);
                if (title != NULL) {
                    SysFreeString(title);
                }
            }
        }
        entry->lpVtbl->Release(entry);
    }

    history->lpVtbl->Release(history);
    searcher->lpVtbl->Release(searcher);
    session->lpVtbl->Release(session);
    wt_com_end(&com);

    return status->last_install_title[0] != L'\0' ? WT_OK : WT_ERR_NOT_FOUND;
}

WT_Result wt_collect_update_status_fast(WT_UpdateStatus *status)
{
    if (status == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_update_status_init(status);

    wt_collect_reboot_flags(status);
    wt_collect_last_times(status);
    status->wu_service_running = wt_query_wu_service_running();

    (void)wt_collect_last_install_title(status);
    return WT_OK;
}

static void wt_add_pending_update(WT_UpdateStatus *status, IUpdate *update)
{
    if (status->pending_count >= WT_MAX_PENDING_UPDATES || update == NULL) {
        return;
    }

    WT_PendingUpdate *p = &status->pending[status->pending_count];
    ZeroMemory(p, sizeof(*p));

    BSTR title = NULL;
    if (SUCCEEDED(update->lpVtbl->get_Title(update, &title))) {
        wt_bstr_copy_w(p->title, ARRAYSIZE(p->title), title);
        if (title != NULL) {
            SysFreeString(title);
        }
    }

    IUpdateIdentity *identity = NULL;
    if (SUCCEEDED(update->lpVtbl->get_Identity(update, &identity)) &&
        identity != NULL) {
        BSTR uid = NULL;
        if (SUCCEEDED(identity->lpVtbl->get_UpdateID(identity, &uid))) {
            wt_bstr_copy_w(p->update_id, ARRAYSIZE(p->update_id), uid);
            if (uid != NULL) {
                SysFreeString(uid);
            }
        }
        identity->lpVtbl->Release(identity);
    }

    VARIANT_BOOL mand = VARIANT_FALSE;
    if (SUCCEEDED(update->lpVtbl->get_IsMandatory(update, &mand))) {
        p->mandatory = (mand == VARIANT_TRUE) ? 1 : 0;
    }

    VARIANT_BOOL downloaded = VARIANT_FALSE;
    if (SUCCEEDED(update->lpVtbl->get_IsDownloaded(update, &downloaded))) {
        p->downloaded = (downloaded == VARIANT_TRUE) ? 1 : 0;
    }

    VARIANT_BOOL reboot = VARIANT_FALSE;
    if (SUCCEEDED(update->lpVtbl->get_RebootRequired(update, &reboot))) {
        p->reboot_required = (reboot == VARIANT_TRUE) ? 1 : 0;
    }

    status->pending_count++;
    if (p->mandatory) {
        status->pending_mandatory_count++;
    }
    if (p->reboot_required) {
        status->pending_reboot_count++;
    }
}

WT_Result wt_search_pending_updates(WT_UpdateStatus *status)
{
    WT_ComScope com;
    WT_Result r = wt_com_begin(&com);
    if (r != WT_OK) {
        return r;
    }

    IUpdateSession *session = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUpdateSession, (void **)&session);
    if (FAILED(hr) || session == NULL) {
        wt_com_end(&com);
        StringCchCopyW(status->note, ARRAYSIZE(status->note),
                       L"Windows Update Agent is unavailable.");
        return WT_ERR_WIN32;
    }

    IUpdateSearcher *searcher = NULL;
    hr = session->lpVtbl->CreateUpdateSearcher(session, &searcher);
    if (FAILED(hr) || searcher == NULL) {
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        StringCchCopyW(status->note, ARRAYSIZE(status->note),
                       L"Could not create update searcher.");
        return WT_ERR_WIN32;
    }

    BSTR app_id = SysAllocString(L"WinTune");
    if (app_id != NULL) {
        (void)searcher->lpVtbl->put_ClientApplicationID(searcher, app_id);
        SysFreeString(app_id);
    }

    (void)searcher->lpVtbl->put_Online(searcher, VARIANT_FALSE);

    BSTR criteria = SysAllocString(L"IsInstalled=0 and IsHidden=0");
    if (criteria == NULL) {
        searcher->lpVtbl->Release(searcher);
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        return WT_ERR_OUT_OF_MEMORY;
    }

    ISearchResult *result = NULL;
    hr = searcher->lpVtbl->Search(searcher, criteria, &result);
    SysFreeString(criteria);

    if (FAILED(hr) || result == NULL) {
        searcher->lpVtbl->Release(searcher);
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
            StringCchCopyW(status->note, ARRAYSIZE(status->note),
                           L"Access denied querying Windows Update.");
            return WT_ERR_ACCESS_DENIED;
        }
        StringCchCopyW(status->note, ARRAYSIZE(status->note),
                       L"Windows Update search failed.");
        return WT_ERR_WIN32;
    }

    OperationResultCode rc = orcNotStarted;
    result->lpVtbl->get_ResultCode(result, &rc);
    if (rc != orcSucceeded && rc != orcSucceededWithErrors) {
        result->lpVtbl->Release(result);
        searcher->lpVtbl->Release(searcher);
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        StringCchCopyW(status->note, ARRAYSIZE(status->note),
                       L"Windows Update search did not succeed.");
        return WT_ERR_UNKNOWN;
    }

    IUpdateCollection *updates = NULL;
    hr = result->lpVtbl->get_Updates(result, &updates);
    result->lpVtbl->Release(result);
    if (FAILED(hr) || updates == NULL) {
        searcher->lpVtbl->Release(searcher);
        session->lpVtbl->Release(session);
        wt_com_end(&com);
        return WT_ERR_WIN32;
    }

    LONG count = 0;
    updates->lpVtbl->get_Count(updates, &count);
    for (LONG i = 0; i < count && status->pending_count < WT_MAX_PENDING_UPDATES; ++i) {
        IUpdate *update = NULL;
        if (FAILED(updates->lpVtbl->get_Item(updates, i, &update)) ||
            update == NULL) {
            continue;
        }
        wt_add_pending_update(status, update);
        update->lpVtbl->Release(update);
    }

    updates->lpVtbl->Release(updates);
    searcher->lpVtbl->Release(searcher);
    session->lpVtbl->Release(session);
    wt_com_end(&com);

    status->search_available = 1;
    if (status->pending_reboot_count > 0) {
        status->reboot_required = 1;
    }
    return WT_OK;
}

WT_Result wt_collect_update_status(WT_UpdateStatus *status)
{
    WT_Result r = wt_collect_update_status_fast(status);
    if (r != WT_OK) {
        return r;
    }

    WT_Result sr = wt_search_pending_updates(status);
    if (sr != WT_OK && status->note[0] == L'\0') {
        StringCchCopyW(status->note, ARRAYSIZE(status->note),
                       L"Pending update search was partial; reboot flags still shown.");
    }
    return WT_OK;
}
