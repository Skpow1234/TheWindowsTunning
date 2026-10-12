#include "metrics/memory.h"
#include "metrics/pdh_utils.h"

#include <string.h>
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

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
    out->standby_ok = 0;
    out->standby_bytes = 0;
    out->modified_ok = 0;
    out->modified_bytes = 0;
    out->compression_ok = 0;
    out->compression_bytes = 0;
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

static int wt_memory_counter_bytes(PDH_HCOUNTER counter, unsigned long long cap,
                                   unsigned long long *out_bytes)
{
    PDH_FMT_COUNTERVALUE value;
    DWORD value_type = 0;
    PDH_STATUS st;

    if (counter == NULL || out_bytes == NULL) {
        return 0;
    }
    memset(&value, 0, sizeof(value));
    st = PdhGetFormattedCounterValue(counter, PDH_FMT_LARGE, &value_type, &value);
    if (st != ERROR_SUCCESS) {
        return 0;
    }
    if (value.CStatus != PDH_CSTATUS_VALID_DATA &&
        value.CStatus != PDH_CSTATUS_NEW_DATA) {
        return 0;
    }
    if (value.largeValue < 0) {
        return 0;
    }
    if (cap > 0 && (unsigned long long)value.largeValue > cap) {
        return 0;
    }
    *out_bytes = (unsigned long long)value.largeValue;
    return 1;
}

/* Standby is the sum of the three public cache lists. Modified is its own
 * counter. One extra collect covers providers that reject the first sample.
 * No sleep: these are sizes, not rates. */
static void wt_memory_read_lists(WT_MemoryMetrics *out)
{
    static const wchar_t *k_standby[] = {
        L"\\Memory\\Standby Cache Reserve Bytes",
        L"\\Memory\\Standby Cache Normal Priority Bytes",
        L"\\Memory\\Standby Cache Core Bytes"
    };
    PDH_HQUERY query = NULL;
    PDH_HCOUNTER standby[3] = {NULL, NULL, NULL};
    PDH_HCOUNTER modified = NULL;
    PDH_HCOUNTER compression = NULL;
    unsigned long long cap = out->total_physical_bytes;
    unsigned long long sum = 0;
    unsigned long long part = 0;
    int i;
    int standby_parts = 0;

    if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
        return;
    }
    for (i = 0; i < 3; ++i) {
        if (PdhAddEnglishCounterW(query, k_standby[i], 0, &standby[i]) !=
            ERROR_SUCCESS) {
            standby[i] = NULL;
        }
    }
    if (PdhAddEnglishCounterW(query, L"\\Memory\\Modified Page List Bytes", 0,
                              &modified) != ERROR_SUCCESS) {
        modified = NULL;
    }
    if (PdhAddEnglishCounterW(query, L"\\Process(Memory Compression)\\Working Set",
                              0, &compression) != ERROR_SUCCESS) {
        compression = NULL;
    }
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return;
    }
    (void)PdhCollectQueryData(query);

    for (i = 0; i < 3; ++i) {
        if (wt_memory_counter_bytes(standby[i], cap, &part)) {
            sum += part;
            standby_parts++;
        }
    }
    if (standby_parts == 3 && (cap == 0 || sum <= cap)) {
        out->standby_ok = 1;
        out->standby_bytes = sum;
    }
    if (wt_memory_counter_bytes(modified, cap, &part)) {
        out->modified_ok = 1;
        out->modified_bytes = part;
    }
    if (wt_memory_counter_bytes(compression, cap, &part)) {
        out->compression_ok = 1;
        out->compression_bytes = part;
    }
    PdhCloseQuery(query);
}

/* Physical size of the compressed store: the Memory Compression process
 * working set. Read-only. Does not empty any working set. */
static void wt_memory_read_compression(WT_MemoryMetrics *out)
{
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    DWORD pid = 0;
    HANDLE process;
    PROCESS_MEMORY_COUNTERS counters;
    unsigned long long cap = out->total_physical_bytes;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Memory Compression") == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (pid == 0) {
        return;
    }

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == NULL) {
        return;
    }
    memset(&counters, 0, sizeof(counters));
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(process, &counters, sizeof(counters)) &&
        counters.WorkingSetSize > 0 &&
        (cap == 0 || counters.WorkingSetSize <= cap)) {
        out->compression_ok = 1;
        out->compression_bytes = counters.WorkingSetSize;
    }
    CloseHandle(process);
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
    wt_memory_read_lists(out);
    if (!out->compression_ok) {
        wt_memory_read_compression(out);
    }
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
