#ifndef WINTUNE_LOG_H
#define WINTUNE_LOG_H

/* Logging levels, ordered from most to least severe. The active level acts as
 * a threshold: messages with a level <= the active level are emitted. */
typedef enum WT_LogLevel {
    WT_LOG_ERROR = 0,
    WT_LOG_WARN,
    WT_LOG_INFO,
    WT_LOG_DEBUG,
    WT_LOG_TRACE
} WT_LogLevel;

/* Default level is WT_LOG_WARN so normal CLI output stays clean.
 * --verbose raises it to INFO, --debug raises it to DEBUG. */
void wt_log_set_level(WT_LogLevel level);
WT_LogLevel wt_log_get_level(void);

/* Writes a formatted log line to stderr (never stdout, so JSON output on
 * stdout is never polluted). A trailing newline is added automatically. */
void wt_log(WT_LogLevel level, const char *fmt, ...);

#define WT_LOGE(...) wt_log(WT_LOG_ERROR, __VA_ARGS__)
#define WT_LOGW(...) wt_log(WT_LOG_WARN, __VA_ARGS__)
#define WT_LOGI(...) wt_log(WT_LOG_INFO, __VA_ARGS__)
#define WT_LOGD(...) wt_log(WT_LOG_DEBUG, __VA_ARGS__)
#define WT_LOGT(...) wt_log(WT_LOG_TRACE, __VA_ARGS__)

#endif /* WINTUNE_LOG_H */
