#ifndef WINTUNE_LOG_H
#define WINTUNE_LOG_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Logging levels, ordered from most to least severe. The active level acts as
 * a threshold: messages with a level <= the active level are emitted. */
typedef enum WT_LogLevel {
    WT_LOG_ERROR = 0,
    WT_LOG_WARN,
    WT_LOG_INFO,
    WT_LOG_DEBUG,
    WT_LOG_TRACE
} WT_LogLevel;

void wt_log_set_level(WT_LogLevel level);
WT_LogLevel wt_log_get_level(void);

/* Optional file sink (Phase 18). When open, log lines are appended to the file
 * as well as stderr. Pass NULL to wt_log_close_file() only. */
WT_Result wt_log_open_file(const wchar_t *path);
void wt_log_close_file(void);
int wt_log_file_is_open(void);

void wt_log(WT_LogLevel level, const char *fmt, ...);

#define WT_LOGE(...) wt_log(WT_LOG_ERROR, __VA_ARGS__)
#define WT_LOGW(...) wt_log(WT_LOG_WARN, __VA_ARGS__)
#define WT_LOGI(...) wt_log(WT_LOG_INFO, __VA_ARGS__)
#define WT_LOGD(...) wt_log(WT_LOG_DEBUG, __VA_ARGS__)
#define WT_LOGT(...) wt_log(WT_LOG_TRACE, __VA_ARGS__)

#endif /* WINTUNE_LOG_H */
