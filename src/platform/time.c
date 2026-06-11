#include "platform/time.h"

#include <windows.h>
#include <stdio.h>

WT_Result wt_now_iso8601_utc(char *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    SYSTEMTIME st;
    GetSystemTime(&st); /* always UTC */

    int written = snprintf(out, count,
                           "%04u-%02u-%02uT%02u:%02u:%02uZ",
                           (unsigned)st.wYear, (unsigned)st.wMonth,
                           (unsigned)st.wDay, (unsigned)st.wHour,
                           (unsigned)st.wMinute, (unsigned)st.wSecond);

    if (written < 0 || (size_t)written >= count) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}
