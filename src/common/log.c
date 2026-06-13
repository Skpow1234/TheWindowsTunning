#include "common/log.h"

#include "platform/paths.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static WT_LogLevel g_log_level = WT_LOG_WARN;
static FILE *g_log_file = NULL;
static int g_log_atexit_registered = 0;

static void wt_log_file_atexit(void)
{
    wt_log_close_file();
}

static void wt_log_ensure_parent_dir(const wchar_t *path)
{
    if (path == NULL) {
        return;
    }
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, path, _TRUNCATE);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash == NULL) {
        return;
    }
    *slash = L'\0';
    if (dir[0] != L'\0') {
        (void)wt_paths_ensure_dir(dir);
    }
}

void wt_log_set_level(WT_LogLevel level)
{
    g_log_level = level;
}

WT_LogLevel wt_log_get_level(void)
{
    return g_log_level;
}

WT_Result wt_log_open_file(const wchar_t *path)
{
    wt_log_close_file();
    if (path == NULL || path[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wt_log_ensure_parent_dir(path);

    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"a") != 0 || f == NULL) {
        return WT_ERR_WIN32;
    }

    g_log_file = f;

    if (!g_log_atexit_registered) {
        atexit(wt_log_file_atexit);
        g_log_atexit_registered = 1;
    }
    return WT_OK;
}

void wt_log_close_file(void)
{
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

int wt_log_file_is_open(void)
{
    return g_log_file != NULL ? 1 : 0;
}

static const char *wt_log_level_tag(WT_LogLevel level)
{
    switch (level) {
    case WT_LOG_ERROR: return "ERROR";
    case WT_LOG_WARN:  return "WARN";
    case WT_LOG_INFO:  return "INFO";
    case WT_LOG_DEBUG: return "DEBUG";
    case WT_LOG_TRACE: return "TRACE";
    default:           return "LOG";
    }
}

static void wt_log_write_line(WT_LogLevel level, const char *fmt, va_list args)
{
    if (fmt == NULL) {
        return;
    }

    char body[2048];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(body, sizeof(body), fmt, copy);
    va_end(copy);

    FILE *targets[2];
    size_t target_count = 0;
    targets[target_count++] = stderr;
    if (g_log_file != NULL) {
        targets[target_count++] = g_log_file;
    }

    for (size_t i = 0; i < target_count; ++i) {
        FILE *out = targets[i];
        fprintf(out, "[%s] %s\n", wt_log_level_tag(level), body);
        if (g_log_file != NULL && out == g_log_file) {
            fflush(g_log_file);
        }
    }
}

void wt_log(WT_LogLevel level, const char *fmt, ...)
{
    if (fmt == NULL || level > g_log_level) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    wt_log_write_line(level, fmt, args);
    va_end(args);
}
