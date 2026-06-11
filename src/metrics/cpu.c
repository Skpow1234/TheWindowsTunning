#include "metrics/cpu.h"
#include "metrics/pdh_utils.h"

#include <windows.h>

#define WT_CPU_DEFAULT_SAMPLE_MS 500

WT_Result wt_collect_cpu_metrics(unsigned int sample_ms, WT_CpuMetrics *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    out->total_usage_percent = -1.0;
    out->available = 0;

    SYSTEM_INFO info;
    GetSystemInfo(&info);
    out->logical_processor_count = info.dwNumberOfProcessors;

    if (sample_ms == 0) {
        sample_ms = WT_CPU_DEFAULT_SAMPLE_MS;
    }

    double usage = 0.0;
    WT_Result r = wt_pdh_sample_single(L"\\Processor(_Total)\\% Processor Time",
                                       sample_ms, &usage);
    if (r != WT_OK) {
        return r;
    }

    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    out->total_usage_percent = usage;
    out->available = 1;
    return WT_OK;
}
