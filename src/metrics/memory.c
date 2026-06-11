#include "metrics/memory.h"

#include <windows.h>

WT_Result wt_collect_memory_metrics(WT_MemoryMetrics *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return WT_ERR_WIN32;
    }

    out->total_physical_bytes = status.ullTotalPhys;
    out->available_physical_bytes = status.ullAvailPhys;
    out->used_physical_bytes =
        (status.ullTotalPhys >= status.ullAvailPhys)
            ? (status.ullTotalPhys - status.ullAvailPhys)
            : 0ULL;
    out->used_percent =
        (status.ullTotalPhys > 0)
            ? ((double)out->used_physical_bytes * 100.0 / (double)status.ullTotalPhys)
            : 0.0;

    return WT_OK;
}
