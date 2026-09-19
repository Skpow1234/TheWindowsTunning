#include "metrics/memory.h"
#include "metrics/pdh_utils.h"

#include <string.h>
#include <windows.h>
#include <psapi.h>

static void wt_memory_clear_optional(WT_MemoryMetrics *out)
{
    out->commit_ok = 0;
    out->commit_total_bytes = 0;
    out->commit_limit_bytes = 0;
    out->commit_peak_bytes = 0;
    out->commit_percent = 0.0;
    out->pagefile_ok = 0;
    out->commit_available_bytes = 0;
    out->hard_faults_ok = 0;
    out->hard_faults_per_sec = -1.0;
    out->page_faults_ok = 0;
    out->page_faults_per_sec = -1.0;
}

static void wt_memory_fill_commit(WT_MemoryMetrics *out,
                                  const MEMORYSTATUSEX *status)
{
    PERFORMANCE_INFORMATION pi;

    if (status != NULL) {
        out->pagefile_ok = 1;
        out->commit_available_bytes = status->ullAvailPageFile;
        if (!out->commit_ok && status->ullTotalPageFile > 0) {
            out->commit_limit_bytes = status->ullTotalPageFile;
            if (status->ullTotalPageFile >= status->ullAvailPageFile) {
                out->commit_total_bytes =
                    status->ullTotalPageFile - status->ullAvailPageFile;
            }
            out->commit_percent =
                (double)out->commit_total_bytes * 100.0 /
                (double)status->ullTotalPageFile;
            out->commit_ok = 1;
        }
    }

    memset(&pi, 0, sizeof(pi));
    pi.cb = sizeof(pi);
    if (!GetPerformanceInfo(&pi, sizeof(pi))) {
        return;
    }

    {
        unsigned long long page = (unsigned long long)pi.PageSize;
        if (page == 0) {
            page = 4096ull;
        }
        out->commit_ok = 1;
        out->commit_total_bytes = (unsigned long long)pi.CommitTotal * page;
        out->commit_limit_bytes = (unsigned long long)pi.CommitLimit * page;
        out->commit_peak_bytes = (unsigned long long)pi.CommitPeak * page;
        if (out->commit_limit_bytes > 0) {
            out->commit_percent =
                (double)out->commit_total_bytes * 100.0 /
                (double)out->commit_limit_bytes;
        }
    }
}

WT_Result wt_collect_memory_metrics(WT_MemoryMetrics *out)
{
    MEMORYSTATUSEX status;

    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    memset(out, 0, sizeof(*out));
    wt_memory_clear_optional(out);

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
            ? ((double)out->used_physical_bytes * 100.0 /
               (double)status.ullTotalPhys)
            : 0.0;

    wt_memory_fill_commit(out, &status);
    return WT_OK;
}

WT_Result wt_collect_memory_metrics_ex(WT_MemoryMetrics *out,
                                       unsigned int fault_sample_ms)
{
    WT_Result r = wt_collect_memory_metrics(out);
    double hard = 0.0;
    double all = 0.0;

    if (r != WT_OK || out == NULL) {
        return r;
    }
    if (fault_sample_ms == 0) {
        return WT_OK;
    }

    if (wt_pdh_sample_single(L"\\Memory\\Pages Input/sec", fault_sample_ms,
                             &hard) == WT_OK) {
        out->hard_faults_ok = 1;
        out->hard_faults_per_sec = hard;
    }
    /* Second sample shares the same interval cost if first failed; skip if
     * we already blocked once to keep CLI snappy. Soft+hard rate is optional. */
    if (out->hard_faults_ok) {
        /* Reuse a shorter follow-up sample for total page faults. */
        unsigned follow = fault_sample_ms > 200u ? 200u : fault_sample_ms;
        if (wt_pdh_sample_single(L"\\Memory\\Page Faults/sec", follow,
                                 &all) == WT_OK) {
            out->page_faults_ok = 1;
            out->page_faults_per_sec = all;
        }
    }
    return WT_OK;
}
