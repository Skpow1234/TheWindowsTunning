#include "common/error.h"

#include <windows.h>
#include <strsafe.h>

const char *wt_result_to_string(WT_Result result)
{
    switch (result) {
    case WT_OK:                   return "OK";
    case WT_ERR_UNKNOWN:          return "unknown error";
    case WT_ERR_INVALID_ARGUMENT: return "invalid argument";
    case WT_ERR_OUT_OF_MEMORY:    return "out of memory";
    case WT_ERR_WIN32:            return "Win32 error";
    case WT_ERR_PDH:              return "PDH error";
    case WT_ERR_ACCESS_DENIED:    return "access denied";
    case WT_ERR_NOT_SUPPORTED:    return "not supported";
    case WT_ERR_NOT_FOUND:        return "not found";
    case WT_ERR_BUFFER_TOO_SMALL: return "buffer too small";
    case WT_ERR_TIMEOUT:          return "timeout";
    case WT_ERR_CANCELLED:        return "cancelled";
    default:                      return "unrecognized result";
    }
}

WT_Result wt_format_win32_error(unsigned long error_code,
                                wchar_t *buffer,
                                size_t buffer_count)
{
    if (buffer == NULL || buffer_count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    DWORD written = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        (DWORD)error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        (DWORD)buffer_count,
        NULL);

    if (written == 0) {
        /* No system text available; emit a numeric fallback. */
        StringCchPrintfW(buffer, buffer_count, L"Win32 error %lu", error_code);
        return WT_OK;
    }

    /* Trim trailing carriage returns / line feeds / spaces. */
    while (written > 0) {
        wchar_t c = buffer[written - 1];
        if (c == L'\r' || c == L'\n' || c == L' ' || c == L'\t') {
            buffer[--written] = L'\0';
        } else {
            break;
        }
    }

    return WT_OK;
}
