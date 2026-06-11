#ifndef WINTUNE_ERROR_H
#define WINTUNE_ERROR_H

#include <stddef.h>

/* Project-wide structured result code. Every public function that can fail
 * returns one of these so callers can handle errors explicitly. */
typedef enum WT_Result {
    WT_OK = 0,
    WT_ERR_UNKNOWN,
    WT_ERR_INVALID_ARGUMENT,
    WT_ERR_OUT_OF_MEMORY,
    WT_ERR_WIN32,
    WT_ERR_PDH,
    WT_ERR_ACCESS_DENIED,
    WT_ERR_NOT_SUPPORTED,
    WT_ERR_NOT_FOUND,
    WT_ERR_BUFFER_TOO_SMALL,
    WT_ERR_TIMEOUT,
    WT_ERR_CANCELLED
} WT_Result;

/* Returns a stable, human-readable name for a result code. Never NULL. */
const char *wt_result_to_string(WT_Result result);

/* Formats a Win32 error code (typically from GetLastError()) into a
 * wide-character buffer. Trailing CR/LF from the system message are trimmed.
 * Returns WT_OK on success, WT_ERR_INVALID_ARGUMENT for a NULL/empty buffer. */
WT_Result wt_format_win32_error(unsigned long error_code,
                                wchar_t *buffer,
                                size_t buffer_count);

#endif /* WINTUNE_ERROR_H */
