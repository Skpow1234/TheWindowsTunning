#include "platform/paths.h"

#include <windows.h>
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
