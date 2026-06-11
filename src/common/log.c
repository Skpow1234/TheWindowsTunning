#include "common/log.h"

#include <stdarg.h>
#include <stdio.h>

static WT_LogLevel g_log_level = WT_LOG_WARN;

void wt_log_set_level(WT_LogLevel level)
{
    g_log_level = level;
}

WT_LogLevel wt_log_get_level(void)
{
    return g_log_level;
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

void wt_log(WT_LogLevel level, const char *fmt, ...)
{
    if (fmt == NULL || level > g_log_level) {
        return;
    }

    fprintf(stderr, "[%s] ", wt_log_level_tag(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
