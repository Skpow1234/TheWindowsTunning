#include "service/service_policy.h"
#include "platform/paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <windows.h>

void wt_service_policy_defaults(WT_ServicePolicy *out)
{
    if (out == NULL) {
        return;
    }
    ZeroMemory(out, sizeof(*out));
    StringCchCopyA(out->name, sizeof(out->name), "balanced");
    out->scan_interval_ms = 15u * 60u * 1000u; /* 15 minutes */
    out->sample_count = 3;
    out->top_process_limit = 10;
    out->history_keep = 2;
}

static void wt_service_policy_set(WT_ServicePolicy *out, const char *name,
                                  unsigned interval_ms, unsigned samples,
                                  unsigned top, unsigned history)
{
    ZeroMemory(out, sizeof(*out));
    StringCchCopyA(out->name, sizeof(out->name), name);
    out->scan_interval_ms = interval_ms;
    out->sample_count = samples;
    out->top_process_limit = top;
    out->history_keep = history;
    if (out->history_keep > 8) {
        out->history_keep = 8;
    }
    if (out->sample_count == 0) {
        out->sample_count = 1;
    }
    if (out->top_process_limit == 0) {
        out->top_process_limit = 5;
    }
}

WT_Result wt_service_policy_from_name(const wchar_t *name, WT_ServicePolicy *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (name == NULL || name[0] == L'\0' ||
        _wcsicmp(name, L"balanced") == 0 || _wcsicmp(name, L"default") == 0) {
        wt_service_policy_set(out, "balanced", 15u * 60u * 1000u, 3, 10, 2);
        return WT_OK;
    }
    if (_wcsicmp(name, L"performance") == 0 ||
        _wcsicmp(name, L"perf") == 0) {
        wt_service_policy_set(out, "performance", 5u * 60u * 1000u, 5, 15, 3);
        return WT_OK;
    }
    if (_wcsicmp(name, L"light") == 0 ||
        _wcsicmp(name, L"battery") == 0) {
        wt_service_policy_set(out, "light", 60u * 60u * 1000u, 1, 5, 1);
        return WT_OK;
    }
    if (_wcsicmp(name, L"on-demand") == 0 ||
        _wcsicmp(name, L"ondemand") == 0 ||
        _wcsicmp(name, L"manual") == 0) {
        wt_service_policy_set(out, "on-demand", 0, 3, 10, 1);
        return WT_OK;
    }
    return WT_ERR_NOT_FOUND;
}

WT_Result wt_service_policy_path(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\service_policy.json", dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static int wt_policy_extract_uint(const char *json, const char *key,
                                  unsigned *out)
{
    if (json == NULL || key == NULL || out == NULL) {
        return 0;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return 0;
    }
    const char *colon = strchr(pos + strlen(pattern), ':');
    if (colon == NULL) {
        return 0;
    }
    char *end = NULL;
    unsigned long v = strtoul(colon + 1, &end, 10);
    if (end == colon + 1) {
        return 0;
    }
    *out = (unsigned)v;
    return 1;
}

static int wt_policy_extract_name(const char *json, char *out, size_t cap)
{
    if (json == NULL || out == NULL || cap == 0) {
        return 0;
    }
    const char *pos = strstr(json, "\"name\"");
    if (pos == NULL) {
        return 0;
    }
    const char *colon = strchr(pos, ':');
    if (colon == NULL) {
        return 0;
    }
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) {
        return 0;
    }
    q1++;
    const char *q2 = strchr(q1, '"');
    if (q2 == NULL) {
        return 0;
    }
    size_t n = (size_t)(q2 - q1);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, q1, n);
    out[n] = '\0';
    return 1;
}

WT_Result wt_service_policy_load(WT_ServicePolicy *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_service_policy_defaults(out);

    wchar_t path[MAX_PATH];
    WT_Result r = wt_service_policy_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return WT_OK; /* defaults */
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return WT_OK;
    }

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return WT_OK;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 64 * 1024) {
        CloseHandle(h);
        return WT_OK;
    }
    char *buf = (char *)malloc((size_t)size.QuadPart + 1u);
    if (buf == NULL) {
        CloseHandle(h);
        return WT_ERR_OUT_OF_MEMORY;
    }
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)size.QuadPart, &read, NULL);
    CloseHandle(h);
    if (!ok) {
        free(buf);
        return WT_OK;
    }
    buf[read] = '\0';

    char name[WT_SERVICE_POLICY_NAME_MAX];
    if (wt_policy_extract_name(buf, name, sizeof(name))) {
        StringCchCopyA(out->name, sizeof(out->name), name);
    }
    unsigned v = 0;
    if (wt_policy_extract_uint(buf, "scan_interval_ms", &v)) {
        out->scan_interval_ms = v;
    }
    if (wt_policy_extract_uint(buf, "sample_count", &v) && v > 0) {
        out->sample_count = v;
    }
    if (wt_policy_extract_uint(buf, "top_process_limit", &v) && v > 0) {
        out->top_process_limit = v;
    }
    if (wt_policy_extract_uint(buf, "history_keep", &v)) {
        out->history_keep = v > 8 ? 8 : v;
    }
    free(buf);
    return WT_OK;
}

WT_Result wt_service_policy_save(const WT_ServicePolicy *policy)
{
    if (policy == NULL || policy->name[0] == '\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    r = wt_paths_ensure_dir(dir);
    if (r != WT_OK) {
        return r;
    }

    wchar_t path[MAX_PATH];
    r = wt_service_policy_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }

    char body[512];
    snprintf(body, sizeof(body),
             "{\n"
             "  \"version\": 1,\n"
             "  \"name\": \"%s\",\n"
             "  \"scan_interval_ms\": %u,\n"
             "  \"sample_count\": %u,\n"
             "  \"top_process_limit\": %u,\n"
             "  \"history_keep\": %u\n"
             "}\n",
             policy->name, policy->scan_interval_ms, policy->sample_count,
             policy->top_process_limit, policy->history_keep);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, body, (DWORD)strlen(body), &written, NULL);
    CloseHandle(h);
    return ok ? WT_OK : WT_ERR_WIN32;
}

WT_Result wt_service_policy_remove(void)
{
    wchar_t path[MAX_PATH];
    WT_Result r = wt_service_policy_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return WT_OK;
    }
    if (!DeleteFileW(path)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return WT_OK;
        }
        return WT_ERR_WIN32;
    }
    return WT_OK;
}

WT_Result wt_service_policy_rotate_last_scan(const WT_ServicePolicy *policy)
{
    if (policy == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t current[MAX_PATH];
    WT_Result r = wt_paths_last_scan_file(current, ARRAYSIZE(current));
    if (r != WT_OK) {
        return r;
    }
    if (GetFileAttributesW(current) == INVALID_FILE_ATTRIBUTES) {
        return WT_OK; /* nothing to rotate */
    }

    unsigned keep = policy->history_keep;
    if (keep == 0) {
        return WT_OK; /* overwrite in place without history */
    }

    wchar_t dir[MAX_PATH];
    r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }

    /* Delete oldest slot, then shift .N -> .N+1, then current -> .1 */
    wchar_t path_old[MAX_PATH];
    wchar_t path_new[MAX_PATH];
    if (FAILED(StringCchPrintfW(path_old, ARRAYSIZE(path_old),
                                L"%s\\last_scan.%u.json", dir, keep))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    DeleteFileW(path_old);

    for (unsigned i = keep; i >= 2; --i) {
        if (FAILED(StringCchPrintfW(path_old, ARRAYSIZE(path_old),
                                    L"%s\\last_scan.%u.json", dir, i - 1)) ||
            FAILED(StringCchPrintfW(path_new, ARRAYSIZE(path_new),
                                    L"%s\\last_scan.%u.json", dir, i))) {
            return WT_ERR_BUFFER_TOO_SMALL;
        }
        MoveFileExW(path_old, path_new, MOVEFILE_REPLACE_EXISTING);
    }

    if (FAILED(StringCchPrintfW(path_new, ARRAYSIZE(path_new),
                                L"%s\\last_scan.1.json", dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    if (!MoveFileExW(current, path_new, MOVEFILE_REPLACE_EXISTING)) {
        /* Non-fatal: caller can still overwrite current. */
        return WT_OK;
    }
    return WT_OK;
}

const char *wt_service_policy_describe(const WT_ServicePolicy *policy)
{
    if (policy == NULL) {
        return "unknown";
    }
    if (strcmp(policy->name, "performance") == 0) {
        return "Frequent scans (5 min), deeper samples";
    }
    if (strcmp(policy->name, "light") == 0) {
        return "Infrequent scans (60 min), light samples";
    }
    if (strcmp(policy->name, "on-demand") == 0) {
        return "No periodic scans; start + IPC only";
    }
    return "Balanced periodic scans (15 min)";
}
