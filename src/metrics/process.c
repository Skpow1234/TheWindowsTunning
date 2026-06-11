#include "metrics/process.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <strsafe.h>
#include <stdlib.h>

#include "metrics/process.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <strsafe.h>
#include <stdlib.h>

static void wt_process_fill(WT_ProcessInfo *p, const PROCESSENTRY32W *entry)
{
    ZeroMemory(p, sizeof(*p));
    p->pid = entry->th32ProcessID;
    p->cpu_percent = -1.0;
    StringCchCopyW(p->name, ARRAYSIZE(p->name), entry->szExeFile);

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                              entry->th32ProcessID);
    if (proc != NULL) {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        ZeroMemory(&pmc, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(proc, (PROCESS_MEMORY_COUNTERS *)&pmc,
                                 sizeof(pmc))) {
            p->working_set_bytes = pmc.WorkingSetSize;
            p->private_bytes = pmc.PrivateUsage;
        }

        IO_COUNTERS io;
        ZeroMemory(&io, sizeof(io));
        if (GetProcessIoCounters(proc, &io)) {
            p->read_bytes = io.ReadTransferCount;
            p->write_bytes = io.WriteTransferCount;
        }

        CloseHandle(proc);
    }
}

/* Inserts `candidate` into a descending top-K buffer of length `*filled`.
 * `limit` is the maximum number of entries to retain. */
static void wt_topk_insert(WT_ProcessInfo *top, size_t *filled, size_t limit,
                           const WT_ProcessInfo *candidate)
{
    size_t n = *filled;
    if (n < limit) {
        top[n] = *candidate;
        (*filled)++;
        for (size_t i = n; i > 0 &&
             top[i].working_set_bytes > top[i - 1].working_set_bytes;
             --i) {
            WT_ProcessInfo tmp = top[i - 1];
            top[i - 1] = top[i];
            top[i] = tmp;
        }
        return;
    }

    if (candidate->working_set_bytes <= top[n - 1].working_set_bytes) {
        return;
    }

    top[n - 1] = *candidate;
    for (size_t i = n - 1; i > 0 &&
         top[i].working_set_bytes > top[i - 1].working_set_bytes;
         --i) {
        WT_ProcessInfo tmp = top[i - 1];
        top[i - 1] = top[i];
        top[i] = tmp;
    }
}

WT_Result wt_collect_processes(WT_ProcessInfo *out,
                               size_t capacity,
                               size_t *out_count)
{
    if (out == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);

    WT_Result result = WT_OK;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (*out_count >= capacity) {
                break;
            }

            wt_process_fill(&out[*out_count], &entry);
            (*out_count)++;
        } while (Process32NextW(snapshot, &entry));
    } else {
        result = WT_ERR_WIN32;
    }

    CloseHandle(snapshot);
    return result;
}

WT_Result wt_collect_top_processes_by_memory(WT_ProcessInfo *out,
                                             size_t limit,
                                             size_t *out_count)
{
    if (out == NULL || out_count == NULL || limit == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);

    WT_Result result = WT_OK;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            WT_ProcessInfo info;
            wt_process_fill(&info, &entry);
            wt_topk_insert(out, out_count, limit, &info);
        } while (Process32NextW(snapshot, &entry));
    } else {
        result = WT_ERR_WIN32;
    }

    CloseHandle(snapshot);
    return result;
}

static int wt_compare_by_memory_desc(const void *a, const void *b)
{
    const WT_ProcessInfo *pa = (const WT_ProcessInfo *)a;
    const WT_ProcessInfo *pb = (const WT_ProcessInfo *)b;
    if (pa->working_set_bytes < pb->working_set_bytes) return 1;
    if (pa->working_set_bytes > pb->working_set_bytes) return -1;
    return 0;
}

void wt_sort_processes_by_memory(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL || count < 2) {
        return;
    }
    qsort(items, count, sizeof(WT_ProcessInfo), wt_compare_by_memory_desc);
}
