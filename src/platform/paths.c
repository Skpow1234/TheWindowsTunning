#include "platform/paths.h"

#include <windows.h>
#include <shlobj.h>
#include <strsafe.h>

WT_Result wt_paths_user_data_dir(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    if (n == 0 || n >= ARRAYSIZE(base)) {
        return WT_ERR_NOT_FOUND;
    }

    if (FAILED(StringCchPrintfW(out, count, L"%s\\WinTune", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_paths_rollback_dir(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t base[MAX_PATH];
    WT_Result r = wt_paths_user_data_dir(base, ARRAYSIZE(base));
    if (r != WT_OK) {
        return r;
    }

    if (FAILED(StringCchPrintfW(out, count, L"%s\\rollback", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_paths_ensure_dir(const wchar_t *dir)
{
    if (dir == NULL || dir[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    /* Walk the path creating each component. Skip the drive prefix (e.g.
     * "C:\"). CreateDirectoryW returning ALREADY_EXISTS is treated as success. */
    wchar_t buf[MAX_PATH];
    if (FAILED(StringCchCopyW(buf, ARRAYSIZE(buf), dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    for (wchar_t *p = buf; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            /* Don't try to create the bare drive root ("C:\"). */
            if (p == buf || *(p - 1) == L':') {
                continue;
            }
            wchar_t saved = *p;
            *p = L'\0';
            if (!CreateDirectoryW(buf, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                *p = saved;
                return WT_ERR_WIN32;
            }
            *p = saved;
        }
    }

    if (!CreateDirectoryW(buf, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return WT_ERR_WIN32;
    }
    return WT_OK;
}

WT_Result wt_paths_program_data_dir(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", base, ARRAYSIZE(base));
    if (n == 0 || n >= ARRAYSIZE(base)) {
        return WT_ERR_NOT_FOUND;
    }

    if (FAILED(StringCchPrintfW(out, count, L"%s\\WinTune", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_paths_last_scan_file(wchar_t *out, size_t count)
{
    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\last_scan.json", dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_paths_reports_dir(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t docs[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs))) {
        return WT_ERR_NOT_FOUND;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\WinTune\\Reports", docs))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static int wt_paths_is_report_ext(const wchar_t *name)
{
    if (name == NULL) {
        return 0;
    }
    const wchar_t *dot = wcsrchr(name, L'.');
    if (dot == NULL) {
        return 0;
    }
    return (_wcsicmp(dot, L".txt") == 0 || _wcsicmp(dot, L".json") == 0);
}

WT_Result wt_paths_newest_report(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out[0] = L'\0';

    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_reports_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }

    wchar_t pattern[MAX_PATH];
    if (FAILED(StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\*", dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return WT_ERR_NOT_FOUND;
    }

    ULONGLONG best_time = 0;
    wchar_t best_path[MAX_PATH] = {0};
    int found = 0;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!wt_paths_is_report_ext(fd.cFileName)) {
            continue;
        }
        ULARGE_INTEGER uli;
        uli.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (!found || uli.QuadPart > best_time) {
            best_time = uli.QuadPart;
            if (FAILED(StringCchPrintfW(best_path, ARRAYSIZE(best_path),
                                        L"%s\\%s", dir, fd.cFileName))) {
                continue;
            }
            found = 1;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (!found) {
        return WT_ERR_NOT_FOUND;
    }
    if (FAILED(StringCchCopyW(out, count, best_path))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}
