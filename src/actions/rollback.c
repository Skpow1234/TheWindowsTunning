#include "actions/rollback.h"
#include "actions/safe_actions.h"
#include "platform/paths.h"
#include "system/power.h"
#include "system/tasks.h"
#include "common/log.h"

#include <windows.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

/* ---- small UTF-8 / JSON helpers ----------------------------------------- */

static void wt_wide_to_utf8(const wchar_t *w, char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (w == NULL) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    out[cap - 1] = '\0';
}

static void wt_json_write_escaped(FILE *f, const char *s)
{
    for (const char *p = s; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n", f);  break;
        case '\r': fputs("\\r", f);  break;
        case '\t': fputs("\\t", f);  break;
        default:
            if (c < 0x20) {
                fprintf(f, "\\u%04x", c);
            } else {
                fputc((int)c, f);
            }
        }
    }
}

static void wt_json_field_w(FILE *f, const char *key, const wchar_t *val,
                            int trailing_comma)
{
    char utf8[1024];
    wt_wide_to_utf8(val, utf8, sizeof(utf8));
    fprintf(f, "  \"%s\": \"", key);
    wt_json_write_escaped(f, utf8);
    fputs(trailing_comma ? "\",\n" : "\"\n", f);
}

/* Extracts a JSON string value for `key` from an ASCII/UTF-8 buffer. Only
 * unescapes \" and \\; sufficient for the records WinTune writes. Returns 1 on
 * success. */
static int wt_json_get(const char *buf, const char *key, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';

    char pat[96];
    if (FAILED(StringCchPrintfA(pat, sizeof(pat), "\"%s\"", key))) {
        return 0;
    }
    const char *p = strstr(buf, pat);
    if (p == NULL) {
        return 0;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') {
        ++p;
    }
    if (*p != '"') {
        return 0;
    }
    ++p;

    size_t i = 0;
    while (*p != '\0' && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p != '\0') {
            char e = *p++;
            c = (e == 'n') ? '\n' : (e == 't') ? '\t' : (e == 'r') ? '\r' : e;
        }
        if (i + 1 < cap) {
            out[i++] = c;
        }
    }
    out[i] = '\0';
    return 1;
}

/* ---- record I/O ---------------------------------------------------------- */

static WT_Result wt_rollback_record_path(const wchar_t *id, wchar_t *out, size_t cap)
{
    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_rollback_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, cap, L"%s\\%s.json", dir, id))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_rollback_write(WT_RollbackRecord *rec)
{
    if (rec == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_rollback_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    r = wt_paths_ensure_dir(dir);
    if (r != WT_OK) {
        return r;
    }

    SYSTEMTIME lt;
    GetLocalTime(&lt);
    StringCchPrintfW(rec->id, ARRAYSIZE(rec->id),
                     L"%04u%02u%02u-%02u%02u%02u-%03u",
                     lt.wYear, lt.wMonth, lt.wDay,
                     lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);

    SYSTEMTIME ut;
    GetSystemTime(&ut);
    StringCchPrintfA(rec->timestamp_utc, sizeof(rec->timestamp_utc),
                     "%04u-%02u-%02uT%02u:%02u:%02uZ",
                     ut.wYear, ut.wMonth, ut.wDay,
                     ut.wHour, ut.wMinute, ut.wSecond);

    wchar_t path[MAX_PATH];
    r = wt_rollback_record_path(rec->id, path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }

    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"wb") != 0 || f == NULL) {
        return WT_ERR_WIN32;
    }

    wchar_t cmd[128];
    StringCchPrintfW(cmd, ARRAYSIZE(cmd), L"wintune rollback apply %s", rec->id);

    fputs("{\n", f);
    wt_json_field_w(f, "id", rec->id, 1);
    fprintf(f, "  \"timestamp_utc\": \"%s\",\n", rec->timestamp_utc);
    wt_json_field_w(f, "action_id", rec->action_id, 1);
    wt_json_field_w(f, "action_type", rec->action_type, 1);
    wt_json_field_w(f, "description", rec->description, 1);
    wt_json_field_w(f, "previous_value", rec->previous_value, 1);
    wt_json_field_w(f, "new_value", rec->new_value, 1);
    wt_json_field_w(f, "rollback_command", cmd, 0);
    fputs("}\n", f);

    fclose(f);
    return WT_OK;
}

static char *wt_read_file_utf8(const wchar_t *path)
{
    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"rb") != 0 || f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0 || size > (1 << 20)) { /* 1 MiB sanity cap */
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

WT_Result wt_rollback_list(FILE *out, int json)
{
    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_rollback_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }

    wchar_t pattern[MAX_PATH];
    if (FAILED(StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\*.json", dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);

    if (json) {
        fputs("[\n", out);
    } else {
        fputs("Rollback Records\n\n", out);
    }

    int any = 0;
    int first = 1;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            wchar_t path[MAX_PATH];
            if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\%s",
                                        dir, fd.cFileName))) {
                continue;
            }
            char *buf = wt_read_file_utf8(path);
            if (buf == NULL) {
                continue;
            }

            char id[64] = {0}, ts[40] = {0}, type[80] = {0}, desc[256] = {0};
            wt_json_get(buf, "id", id, sizeof(id));
            wt_json_get(buf, "timestamp_utc", ts, sizeof(ts));
            wt_json_get(buf, "action_type", type, sizeof(type));
            wt_json_get(buf, "description", desc, sizeof(desc));
            free(buf);

            any = 1;
            if (json) {
                if (!first) {
                    fputs(",\n", out);
                }
                first = 0;
                fputs("  {\n", out);
                fprintf(out, "    \"id\": \"%s\",\n", id);
                fprintf(out, "    \"timestamp_utc\": \"%s\",\n", ts);
                fprintf(out, "    \"action_type\": \"%s\",\n", type);
                fprintf(out, "    \"description\": \"");
                wt_json_write_escaped(out, desc);
                fputs("\"\n  }", out);
            } else {
                fprintf(out, "  %-22s %-20s %s\n", id, ts, desc);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    if (json) {
        fputs(any ? "\n]\n" : "]\n", out);
    } else if (!any) {
        fputs("  No rollback records found.\n", out);
    } else {
        fputs("\nApply one with: wintune rollback apply <id>\n", out);
    }
    return WT_OK;
}

WT_Result wt_rollback_apply(const wchar_t *id, int assume_yes)
{
    if (id == NULL || id[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t path[MAX_PATH];
    WT_Result r = wt_rollback_record_path(id, path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }
    char *buf = wt_read_file_utf8(path);
    if (buf == NULL) {
        return WT_ERR_NOT_FOUND;
    }

    char type[80] = {0}, prev[256] = {0}, desc[256] = {0};
    wt_json_get(buf, "action_type", type, sizeof(type));
    wt_json_get(buf, "previous_value", prev, sizeof(prev));
    wt_json_get(buf, "description", desc, sizeof(desc));
    free(buf);

    if (strcmp(type, "power_plan_change") == 0) {
        char prompt[384];
        StringCchPrintfA(prompt, sizeof(prompt),
                         "Restore previous power plan (undo: %s)?",
                         desc[0] ? desc : "power plan change");
        if (!wt_action_confirm(prompt, assume_yes)) {
            return WT_ERR_CANCELLED;
        }
        wchar_t wprev[256];
        MultiByteToWideChar(CP_UTF8, 0, prev, -1, wprev, ARRAYSIZE(wprev));
        WT_Result sr = wt_power_set_active_guid_string(wprev);
        if (sr == WT_OK) {
            wt_log(WT_LOG_INFO, "rollback applied: %ls", id);
        }
        return sr;
    }

    if (strcmp(type, "startup_approved") == 0) {
        char prompt[384];
        StringCchPrintfA(prompt, sizeof(prompt),
                         "Revert startup change (undo: %s)?",
                         desc[0] ? desc : "startup change");
        if (!wt_action_confirm(prompt, assume_yes)) {
            return WT_ERR_CANCELLED;
        }

        /* previous_value is "hive|subkey|value_name|byte". */
        wchar_t wprev[512];
        MultiByteToWideChar(CP_UTF8, 0, prev, -1, wprev, ARRAYSIZE(wprev));
        wchar_t *parts[4] = {0};
        int n = 0;
        wchar_t *ctx = NULL;
        for (wchar_t *tok = wcstok_s(wprev, L"|", &ctx);
             tok != NULL && n < 4;
             tok = wcstok_s(NULL, L"|", &ctx)) {
            parts[n++] = tok;
        }
        if (n != 4) {
            return WT_ERR_INVALID_ARGUMENT;
        }
        int enabled = (wcscmp(parts[3], L"02") == 0);
        WT_Result sr = wt_startup_write_approved(parts[0], parts[1], parts[2], enabled);
        if (sr == WT_OK) {
            wt_log(WT_LOG_INFO, "rollback applied: %ls", id);
        }
        return sr;
    }

    if (strcmp(type, "task_enabled") == 0) {
        char prompt[384];
        StringCchPrintfA(prompt, sizeof(prompt),
                         "Revert task enable/disable (undo: %s)?",
                         desc[0] ? desc : "task change");
        if (!wt_action_confirm(prompt, assume_yes)) {
            return WT_ERR_CANCELLED;
        }
        wchar_t wprev[512];
        MultiByteToWideChar(CP_UTF8, 0, prev, -1, wprev, ARRAYSIZE(wprev));
        wchar_t *parts[2] = {0};
        int n = 0;
        wchar_t *ctx = NULL;
        for (wchar_t *tok = wcstok_s(wprev, L"|", &ctx);
             tok != NULL && n < 2;
             tok = wcstok_s(NULL, L"|", &ctx)) {
            parts[n++] = tok;
        }
        if (n != 2) {
            return WT_ERR_INVALID_ARGUMENT;
        }
        int enabled = (wcscmp(parts[1], L"1") == 0);
        WT_Result sr = wt_task_set_enabled(parts[0], enabled);
        if (sr == WT_OK) {
            wt_log(WT_LOG_INFO, "rollback applied: %ls", id);
        }
        return sr;
    }

    if (strcmp(type, "task_delay") == 0) {
        char prompt[384];
        StringCchPrintfA(prompt, sizeof(prompt),
                         "Revert task delay (undo: %s)?",
                         desc[0] ? desc : "task delay");
        if (!wt_action_confirm(prompt, assume_yes)) {
            return WT_ERR_CANCELLED;
        }
        wchar_t wprev[512];
        MultiByteToWideChar(CP_UTF8, 0, prev, -1, wprev, ARRAYSIZE(wprev));
        wchar_t *pipe = wcschr(wprev, L'|');
        if (pipe == NULL) {
            return WT_ERR_INVALID_ARGUMENT;
        }
        *pipe = L'\0';
        const wchar_t *delay_part = pipe + 1;
        unsigned long seconds = 0;
        if (wcsncmp(delay_part, L"delay:", 6) == 0) {
            seconds = wcstoul(delay_part + 6, NULL, 10);
        }
        WT_Result sr = wt_task_set_logon_delay(wprev, seconds);
        if (sr == WT_OK) {
            wt_log(WT_LOG_INFO, "rollback applied: %ls", id);
        }
        return sr;
    }

    if (strcmp(type, "startup_delay") == 0) {
        char prompt[384];
        StringCchPrintfA(prompt, sizeof(prompt),
                         "Revert startup delay (undo: %s)?",
                         desc[0] ? desc : "startup delay");
        if (!wt_action_confirm(prompt, assume_yes)) {
            return WT_ERR_CANCELLED;
        }
        wchar_t wprev[512];
        MultiByteToWideChar(CP_UTF8, 0, prev, -1, wprev, ARRAYSIZE(wprev));
        wchar_t *parts[4] = {0};
        int n = 0;
        wchar_t *ctx = NULL;
        for (wchar_t *tok = wcstok_s(wprev, L"|", &ctx);
             tok != NULL && n < 4;
             tok = wcstok_s(NULL, L"|", &ctx)) {
            parts[n++] = tok;
        }
        if (n < 3) {
            return WT_ERR_INVALID_ARGUMENT;
        }
        WT_Result sr = wt_startup_write_approved(parts[0], parts[1], parts[2], 1);
        if (sr == WT_OK) {
            wt_log(WT_LOG_INFO, "rollback applied: %ls", id);
        }
        return sr;
    }

    return WT_ERR_NOT_SUPPORTED;
}
