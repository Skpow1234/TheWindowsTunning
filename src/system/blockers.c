#include "system/blockers.h"

#include "system/updates.h"

#include <restartmanager.h>
#include <psapi.h>
#include <strsafe.h>

#include <stdlib.h>

#define WT_PENDING_RENAME_KEY \
    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager"

static int wt_pid_seen(const unsigned long *pids, unsigned long count,
                       unsigned long pid)
{
    for (unsigned long i = 0; i < count; ++i) {
        if (pids[i] == pid) {
            return 1;
        }
    }
    return 0;
}

static void wt_normalize_path(wchar_t *path, size_t cap)
{
    if (path == NULL || path[0] == L'\0') {
        return;
    }
    if (wcsncmp(path, L"\\??\\", 4) == 0) {
        wchar_t tmp[512];
        StringCchCopyW(tmp, ARRAYSIZE(tmp), path + 4);
        StringCchCopyW(path, cap, tmp);
    }
}

static WT_Result wt_process_name(unsigned long pid, wchar_t *name, size_t name_cap)
{
    if (name == NULL || name_cap == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    name[0] = L'\0';

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == NULL) {
        return WT_ERR_ACCESS_DENIED;
    }

    wchar_t path[MAX_PATH];
    DWORD path_len = ARRAYSIZE(path);
    if (QueryFullProcessImageNameW(proc, 0, path, &path_len)) {
        const wchar_t *base = wcsrchr(path, L'\\');
        StringCchCopyW(name, name_cap, base != NULL ? base + 1 : path);
    } else {
        _snwprintf_s(name, name_cap, _TRUNCATE, L"pid:%lu", pid);
    }

    CloseHandle(proc);
    return WT_OK;
}

static WT_Result wt_add_process_blocker(WT_BlockerReport *report,
                                        unsigned long pid,
                                        WT_BlockerKind kind,
                                        const wchar_t *app_name,
                                        const wchar_t *reason)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (report->process_blocker_count >= WT_MAX_BLOCKER_PROCESSES) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    unsigned long seen[WT_MAX_BLOCKER_PROCESSES];
    for (unsigned long i = 0; i < report->process_blocker_count; ++i) {
        seen[i] = report->process_blockers[i].pid;
    }
    if (wt_pid_seen(seen, report->process_blocker_count, pid)) {
        return WT_OK;
    }

    WT_BlockerProcess *b = &report->process_blockers[report->process_blocker_count++];
    ZeroMemory(b, sizeof(*b));
    b->pid = pid;
    b->kind = kind;
    (void)wt_process_name(pid, b->name, ARRAYSIZE(b->name));
    if (app_name != NULL && app_name[0] != L'\0') {
        StringCchCopyW(b->app_name, ARRAYSIZE(b->app_name), app_name);
    }
    if (reason != NULL && reason[0] != L'\0') {
        StringCchCopyW(b->reason, ARRAYSIZE(b->reason), reason);
    }
    return WT_OK;
}

static WT_Result wt_read_pending_rename_paths(wchar_t paths[][512],
                                              unsigned long max_paths,
                                              unsigned long *out_count)
{
    if (paths == NULL || out_count == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    HKEY key = NULL;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, WT_PENDING_RENAME_KEY, 0,
                            KEY_READ, &key);
    if (rc != ERROR_SUCCESS) {
        return WT_ERR_NOT_FOUND;
    }

    DWORD type = 0;
    DWORD size = 0;
    rc = RegQueryValueExW(key, L"PendingFileRenameOperations", NULL, &type, NULL,
                          &size);
    if (rc != ERROR_SUCCESS || type != REG_MULTI_SZ || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return WT_ERR_NOT_FOUND;
    }

    wchar_t *blob = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (blob == NULL) {
        RegCloseKey(key);
        return WT_ERR_OUT_OF_MEMORY;
    }

    rc = RegQueryValueExW(key, L"PendingFileRenameOperations", NULL, &type,
                          (LPBYTE)blob, &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, blob);
        return WT_ERR_WIN32;
    }

    const wchar_t *p = blob;
    const wchar_t *end = blob + (size / sizeof(wchar_t));
    int take_source = 1;

    while (p < end && *p != L'\0' && *out_count < max_paths) {
        if (take_source && p[0] != L'\0') {
            StringCchCopyW(paths[*out_count], 512, p);
            wt_normalize_path(paths[*out_count], 512);
            if (paths[*out_count][0] != L'\0') {
                (*out_count)++;
            }
        }
        p += wcslen(p) + 1;
        take_source = !take_source;
    }

    HeapFree(GetProcessHeap(), 0, blob);
    return (*out_count > 0) ? WT_OK : WT_ERR_NOT_FOUND;
}

static WT_Result wt_locked_file_from_rm(const wchar_t *file_path,
                                        WT_BlockerReport *report)
{
    if (file_path == NULL || report == NULL || file_path[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (report->locked_file_count >= WT_MAX_LOCKED_FILES) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    DWORD session = 0;
    wchar_t session_key[CCH_RM_SESSION_KEY + 1];
    DWORD rm_rc = RmStartSession(&session, 0, session_key);
    if (rm_rc != ERROR_SUCCESS) {
        return WT_ERR_WIN32;
    }

    LPCWSTR name = file_path;
    rm_rc = RmRegisterResources(session, 1, &name, 0, NULL, 0, NULL);
    if (rm_rc != ERROR_SUCCESS) {
        RmEndSession(session);
        return WT_ERR_WIN32;
    }

    UINT needed = 0;
    UINT count = 0;
    DWORD reboot_reasons = 0;
    rm_rc = RmGetList(session, &needed, &count, NULL, &reboot_reasons);
    if (rm_rc != ERROR_SUCCESS && rm_rc != ERROR_MORE_DATA) {
        RmEndSession(session);
        return WT_ERR_WIN32;
    }

    RM_PROCESS_INFO *procs = NULL;
    if (needed > 0) {
        procs = (RM_PROCESS_INFO *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY,
            (SIZE_T)needed * sizeof(RM_PROCESS_INFO));
        if (procs == NULL) {
            RmEndSession(session);
            return WT_ERR_OUT_OF_MEMORY;
        }
        count = needed;
        rm_rc = RmGetList(session, &needed, &count, procs, &reboot_reasons);
        if (rm_rc != ERROR_SUCCESS) {
            HeapFree(GetProcessHeap(), 0, procs);
            RmEndSession(session);
            return WT_ERR_WIN32;
        }
    }

    if (count == 0) {
        if (procs != NULL) {
            HeapFree(GetProcessHeap(), 0, procs);
        }
        RmEndSession(session);
        return WT_OK;
    }

    WT_LockedFile *lf = &report->locked_files[report->locked_file_count];
    ZeroMemory(lf, sizeof(*lf));
    StringCchCopyW(lf->path, ARRAYSIZE(lf->path), file_path);

    for (UINT pi = 0; pi < count; ++pi) {
        const RM_PROCESS_INFO *info = &procs[pi];
        unsigned long pid = info->Process.dwProcessId;
        if (pid == 0 || lf->process_count >= WT_MAX_LOCKED_PROCESSES) {
            continue;
        }
        if (wt_pid_seen(lf->pids, lf->process_count, pid)) {
            continue;
        }

        lf->pids[lf->process_count] = pid;
        (void)wt_process_name(pid, lf->process_names[lf->process_count], 260);
        if (info->strAppName[0] != L'\0') {
            StringCchCopyW(lf->process_names[lf->process_count], 260,
                           info->strAppName);
        }
        lf->process_count++;

        wchar_t reason[512];
        StringCchPrintfW(reason, ARRAYSIZE(reason),
                         L"Restart Manager reports this process for locked "
                         L"path: %ls", file_path);
        (void)wt_add_process_blocker(report, pid, WT_BLOCKER_FILE_LOCK,
                                     info->strAppName, reason);
    }

    if (lf->process_count > 0) {
        report->locked_file_count++;
    }

    if (procs != NULL) {
        HeapFree(GetProcessHeap(), 0, procs);
    }
    RmEndSession(session);
    return WT_OK;
}

typedef struct WT_EnumBlockerCtx {
    WT_BlockerReport *report;
} WT_EnumBlockerCtx;

static BOOL CALLBACK wt_enum_shutdown_blockers(HWND hwnd, LPARAM lparam)
{
    WT_EnumBlockerCtx *ctx = (WT_EnumBlockerCtx *)lparam;
    if (ctx == NULL || ctx->report == NULL) {
        return TRUE;
    }
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return TRUE;
    }

    unsigned long seen[WT_MAX_BLOCKER_PROCESSES];
    for (unsigned long i = 0; i < ctx->report->process_blocker_count; ++i) {
        seen[i] = ctx->report->process_blockers[i].pid;
    }
    if (wt_pid_seen(seen, ctx->report->process_blocker_count, pid)) {
        return TRUE;
    }

    DWORD cch = 0;
    if (!ShutdownBlockReasonQuery(hwnd, NULL, &cch)) {
        return TRUE;
    }
    if (cch == 0) {
        return TRUE;
    }

    wchar_t *reason = (wchar_t *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)(cch + 1) * sizeof(wchar_t));
    if (reason == NULL) {
        return TRUE;
    }

    DWORD cch_buf = cch + 1;
    if (!ShutdownBlockReasonQuery(hwnd, reason, &cch_buf)) {
        HeapFree(GetProcessHeap(), 0, reason);
        return TRUE;
    }

    (void)wt_add_process_blocker(ctx->report, pid, WT_BLOCKER_SHUTDOWN, NULL,
                                 reason);
    HeapFree(GetProcessHeap(), 0, reason);
    return TRUE;
}

void wt_blocker_report_init(WT_BlockerReport *report)
{
    if (report == NULL) {
        return;
    }
    ZeroMemory(report, sizeof(*report));
}

WT_Result wt_collect_blockers(WT_BlockerReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_blocker_report_init(report);

    WT_UpdateStatus updates;
    wt_update_status_init(&updates);
    WT_Result upd_r = wt_collect_update_status_fast(&updates);
    if (upd_r == WT_OK) {
        report->reboot_pending = updates.reboot_required;
        report->reboot_wu = updates.reboot_wu;
        report->reboot_cbs = updates.reboot_cbs;
        report->reboot_pending_file_rename = updates.reboot_pending_file_rename;
    }

    WT_EnumBlockerCtx ctx = { .report = report };
    if (!EnumWindows(wt_enum_shutdown_blockers, (LPARAM)&ctx)) {
        StringCchCopyW(report->note, ARRAYSIZE(report->note),
                       L"Could not enumerate top-level windows for shutdown "
                       L"block reasons.");
    }

    wchar_t rename_paths[WT_MAX_LOCKED_FILES][512];
    unsigned long rename_count = 0;
    WT_Result rename_r =
        wt_read_pending_rename_paths(rename_paths, WT_MAX_LOCKED_FILES,
                                     &rename_count);
    report->pending_rename_file_count = rename_count;

    if (rename_r == WT_OK && rename_count > 0) {
        for (unsigned long i = 0; i < rename_count; ++i) {
            (void)wt_locked_file_from_rm(rename_paths[i], report);
        }
    } else if (report->reboot_pending_file_rename && rename_count == 0) {
        if (report->note[0] == L'\0') {
            StringCchCopyW(report->note, ARRAYSIZE(report->note),
                           L"Pending file rename operations are scheduled, "
                           L"but paths could not be read.");
        }
    }

    if (report->process_blocker_count == 0 && report->locked_file_count == 0 &&
        !report->reboot_pending && report->note[0] == L'\0') {
        StringCchCopyW(report->note, ARRAYSIZE(report->note),
                       L"No shutdown-blocking applications or Restart Manager "
                       L"file locks were detected.");
    }

    return WT_OK;
}
