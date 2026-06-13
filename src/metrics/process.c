#include "metrics/process.h"

#include "metrics/pdh_utils.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <strsafe.h>
#include <stdlib.h>

#define WT_PROCESS_DEFAULT_SAMPLE_MS 500

typedef struct WT_ProcessIoSample {
    unsigned long pid;
    unsigned long long read_bytes;
    unsigned long long write_bytes;
} WT_ProcessIoSample;

static void wt_process_init_metrics(WT_ProcessInfo *p)
{
    p->cpu_percent = -1.0;
    p->disk_read_bytes_per_sec = -1.0;
    p->disk_write_bytes_per_sec = -1.0;
}

static void wt_process_fill(WT_ProcessInfo *p, const PROCESSENTRY32W *entry)
{
    ZeroMemory(p, sizeof(*p));
    wt_process_init_metrics(p);
    p->pid = entry->th32ProcessID;
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

static int wt_process_sort_key(const WT_ProcessInfo *p, WT_ProcessSort sort)
{
    switch (sort) {
    case WT_PROCESS_SORT_CPU:
        return (p->cpu_percent >= 0.0) ? 1 : 0;
    case WT_PROCESS_SORT_DISK: {
        double rate = 0.0;
        if (p->disk_read_bytes_per_sec >= 0.0) {
            rate += p->disk_read_bytes_per_sec;
        }
        if (p->disk_write_bytes_per_sec >= 0.0) {
            rate += p->disk_write_bytes_per_sec;
        }
        return (rate > 0.0) ? 1 : 0;
    }
    case WT_PROCESS_SORT_MEMORY:
    default:
        return (p->working_set_bytes > 0) ? 1 : 0;
    }
}

static unsigned long long wt_process_sort_value(const WT_ProcessInfo *p,
                                                WT_ProcessSort sort)
{
    switch (sort) {
    case WT_PROCESS_SORT_CPU:
        return (p->cpu_percent >= 0.0)
                   ? (unsigned long long)(p->cpu_percent * 1000.0)
                   : 0;
    case WT_PROCESS_SORT_DISK: {
        double rate = 0.0;
        if (p->disk_read_bytes_per_sec >= 0.0) {
            rate += p->disk_read_bytes_per_sec;
        }
        if (p->disk_write_bytes_per_sec >= 0.0) {
            rate += p->disk_write_bytes_per_sec;
        }
        return (unsigned long long)rate;
    }
    case WT_PROCESS_SORT_MEMORY:
    default:
        return p->working_set_bytes;
    }
}

static void wt_topk_insert(WT_ProcessInfo *top, size_t *filled, size_t limit,
                           const WT_ProcessInfo *candidate, WT_ProcessSort sort)
{
    if (!wt_process_sort_key(candidate, sort)) {
        return;
    }

    unsigned long long key = wt_process_sort_value(candidate, sort);
    size_t n = *filled;
    if (n < limit) {
        top[n] = *candidate;
        (*filled)++;
        for (size_t i = n; i > 0 &&
             wt_process_sort_value(&top[i], sort) >
                 wt_process_sort_value(&top[i - 1], sort);
             --i) {
            WT_ProcessInfo tmp = top[i - 1];
            top[i - 1] = top[i];
            top[i] = tmp;
        }
        return;
    }

    if (key <= wt_process_sort_value(&top[n - 1], sort)) {
        return;
    }

    top[n - 1] = *candidate;
    for (size_t i = n - 1; i > 0 &&
         wt_process_sort_value(&top[i], sort) >
             wt_process_sort_value(&top[i - 1], sort);
         --i) {
        WT_ProcessInfo tmp = top[i - 1];
        top[i - 1] = top[i];
        top[i] = tmp;
    }
}

static WT_Result wt_sample_process_io(WT_ProcessIoSample *samples,
                                      size_t *sample_count,
                                      size_t capacity)
{
    if (samples == NULL || sample_count == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *sample_count = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    WT_Result result = WT_OK;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (*sample_count >= capacity) {
                break;
            }

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                      entry.th32ProcessID);
            if (proc == NULL) {
                continue;
            }

            IO_COUNTERS io;
            ZeroMemory(&io, sizeof(io));
            if (GetProcessIoCounters(proc, &io)) {
                WT_ProcessIoSample *s = &samples[*sample_count];
                s->pid = entry.th32ProcessID;
                s->read_bytes = io.ReadTransferCount;
                s->write_bytes = io.WriteTransferCount;
                (*sample_count)++;
            }
            CloseHandle(proc);
        } while (Process32NextW(snapshot, &entry));
    } else {
        result = WT_ERR_WIN32;
    }

    CloseHandle(snapshot);
    return result;
}

static const WT_ProcessIoSample *wt_find_io_sample(
    const WT_ProcessIoSample *samples, size_t count, unsigned long pid)
{
    for (size_t i = 0; i < count; ++i) {
        if (samples[i].pid == pid) {
            return &samples[i];
        }
    }
    return NULL;
}

static void wt_apply_pdh_cpu(WT_ProcessInfo *items, size_t count,
                             const unsigned long *pids,
                             const double *cpus, size_t map_count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t m = 0; m < map_count; ++m) {
            if (items[i].pid == pids[m]) {
                items[i].cpu_percent = cpus[m];
                break;
            }
        }
    }
}

WT_Result wt_enrich_process_metrics(WT_ProcessInfo *items,
                                    size_t count,
                                    unsigned int sample_ms)
{
    if (items == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (sample_ms == 0) {
        sample_ms = WT_PROCESS_DEFAULT_SAMPLE_MS;
    }

    WT_ProcessIoSample *before = (WT_ProcessIoSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        count * sizeof(WT_ProcessIoSample));
    WT_ProcessIoSample *after = (WT_ProcessIoSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        count * sizeof(WT_ProcessIoSample));
    if (before == NULL || after == NULL) {
        if (before != NULL) {
            HeapFree(GetProcessHeap(), 0, before);
        }
        if (after != NULL) {
            HeapFree(GetProcessHeap(), 0, after);
        }
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t before_count = 0;
    size_t after_count = 0;
    for (size_t i = 0; i < count; ++i) {
        before[i].pid = items[i].pid;
        before[i].read_bytes = items[i].read_bytes;
        before[i].write_bytes = items[i].write_bytes;
        before_count++;
    }

    Sleep(sample_ms);

    for (size_t i = 0; i < count; ++i) {
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  items[i].pid);
        if (proc == NULL) {
            continue;
        }
        IO_COUNTERS io;
        ZeroMemory(&io, sizeof(io));
        if (GetProcessIoCounters(proc, &io)) {
            after[after_count].pid = items[i].pid;
            after[after_count].read_bytes = io.ReadTransferCount;
            after[after_count].write_bytes = io.WriteTransferCount;
            after_count++;
        }
        CloseHandle(proc);
    }

    const double secs = (double)sample_ms / 1000.0;
    if (secs > 0.0) {
        for (size_t i = 0; i < count; ++i) {
            const WT_ProcessIoSample *a =
                wt_find_io_sample(after, after_count, items[i].pid);
            if (a == NULL) {
                continue;
            }
            unsigned long long dr = 0;
            unsigned long long dw = 0;
            if (a->read_bytes >= before[i].read_bytes) {
                dr = a->read_bytes - before[i].read_bytes;
            }
            if (a->write_bytes >= before[i].write_bytes) {
                dw = a->write_bytes - before[i].write_bytes;
            }
            items[i].disk_read_bytes_per_sec = (double)dr / secs;
            items[i].disk_write_bytes_per_sec = (double)dw / secs;
        }
    }

    unsigned long pdh_pids[512];
    double pdh_cpu[512];
    size_t pdh_count = 0;
    WT_Result cpu_r = wt_pdh_collect_process_cpu(
        sample_ms, pdh_pids, pdh_cpu, ARRAYSIZE(pdh_pids), &pdh_count);
    if (cpu_r == WT_OK) {
        wt_apply_pdh_cpu(items, count, pdh_pids, pdh_cpu, pdh_count);
    }

    HeapFree(GetProcessHeap(), 0, before);
    HeapFree(GetProcessHeap(), 0, after);
    return WT_OK;
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

static int wt_compare_by_memory_desc(const void *a, const void *b)
{
    const WT_ProcessInfo *pa = (const WT_ProcessInfo *)a;
    const WT_ProcessInfo *pb = (const WT_ProcessInfo *)b;
    if (pa->working_set_bytes < pb->working_set_bytes) return 1;
    if (pa->working_set_bytes > pb->working_set_bytes) return -1;
    return 0;
}

static int wt_compare_by_cpu_desc(const void *a, const void *b)
{
    const WT_ProcessInfo *pa = (const WT_ProcessInfo *)a;
    const WT_ProcessInfo *pb = (const WT_ProcessInfo *)b;
    double ca = (pa->cpu_percent >= 0.0) ? pa->cpu_percent : -1.0;
    double cb = (pb->cpu_percent >= 0.0) ? pb->cpu_percent : -1.0;
    if (ca < cb) return 1;
    if (ca > cb) return -1;
    return 0;
}

static int wt_compare_by_disk_desc(const void *a, const void *b)
{
    const WT_ProcessInfo *pa = (const WT_ProcessInfo *)a;
    const WT_ProcessInfo *pb = (const WT_ProcessInfo *)b;
    double da = 0.0;
    double db = 0.0;
    if (pa->disk_read_bytes_per_sec >= 0.0) {
        da += pa->disk_read_bytes_per_sec;
    }
    if (pa->disk_write_bytes_per_sec >= 0.0) {
        da += pa->disk_write_bytes_per_sec;
    }
    if (pb->disk_read_bytes_per_sec >= 0.0) {
        db += pb->disk_read_bytes_per_sec;
    }
    if (pb->disk_write_bytes_per_sec >= 0.0) {
        db += pb->disk_write_bytes_per_sec;
    }
    if (da < db) return 1;
    if (da > db) return -1;
    return 0;
}

WT_Result wt_collect_top_processes(WT_ProcessInfo *out,
                                   size_t limit,
                                   WT_ProcessSort sort,
                                   unsigned int sample_ms,
                                   size_t *out_count)
{
    if (out == NULL || out_count == NULL || limit == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    if (sample_ms == 0 && sort == WT_PROCESS_SORT_MEMORY) {
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
                wt_topk_insert(out, out_count, limit, &info, sort);
            } while (Process32NextW(snapshot, &entry));
        } else {
            result = WT_ERR_WIN32;
        }

        CloseHandle(snapshot);
        if (result == WT_OK) {
            wt_sort_processes(out, *out_count, sort);
        }
        return result;
    }

    if (sample_ms == 0) {
        sample_ms = WT_PROCESS_DEFAULT_SAMPLE_MS;
    }

    WT_ProcessIoSample *io_before = (WT_ProcessIoSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        4096 * sizeof(WT_ProcessIoSample));
    if (io_before == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t io_before_count = 0;
    WT_Result io_r = wt_sample_process_io(io_before, &io_before_count, 4096);
    if (io_r != WT_OK) {
        HeapFree(GetProcessHeap(), 0, io_before);
        return io_r;
    }

    unsigned long pdh_pids[512];
    double pdh_cpu[512];
    size_t pdh_count = 0;
    (void)wt_pdh_collect_process_cpu(sample_ms, pdh_pids, pdh_cpu,
                                     ARRAYSIZE(pdh_pids), &pdh_count);

    WT_ProcessIoSample *io_after = (WT_ProcessIoSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        4096 * sizeof(WT_ProcessIoSample));
    if (io_after == NULL) {
        HeapFree(GetProcessHeap(), 0, io_before);
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t io_after_count = 0;
    (void)wt_sample_process_io(io_after, &io_after_count, 4096);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, io_before);
        HeapFree(GetProcessHeap(), 0, io_after);
        return WT_ERR_WIN32;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);

    WT_Result result = WT_OK;
    const double secs = (double)sample_ms / 1000.0;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            WT_ProcessInfo info;
            wt_process_fill(&info, &entry);

            for (size_t m = 0; m < pdh_count; ++m) {
                if (info.pid == pdh_pids[m]) {
                    info.cpu_percent = pdh_cpu[m];
                    break;
                }
            }

            const WT_ProcessIoSample *b =
                wt_find_io_sample(io_before, io_before_count, info.pid);
            const WT_ProcessIoSample *a =
                wt_find_io_sample(io_after, io_after_count, info.pid);
            if (b != NULL && a != NULL && secs > 0.0) {
                if (a->read_bytes >= b->read_bytes) {
                    info.disk_read_bytes_per_sec =
                        (double)(a->read_bytes - b->read_bytes) / secs;
                } else {
                    info.disk_read_bytes_per_sec = 0.0;
                }
                if (a->write_bytes >= b->write_bytes) {
                    info.disk_write_bytes_per_sec =
                        (double)(a->write_bytes - b->write_bytes) / secs;
                } else {
                    info.disk_write_bytes_per_sec = 0.0;
                }
            }

            wt_topk_insert(out, out_count, limit, &info, sort);
        } while (Process32NextW(snapshot, &entry));
    } else {
        result = WT_ERR_WIN32;
    }

    CloseHandle(snapshot);
    HeapFree(GetProcessHeap(), 0, io_before);
    HeapFree(GetProcessHeap(), 0, io_after);

    if (result == WT_OK) {
        wt_sort_processes(out, *out_count, sort);
    }
    return result;
}

WT_Result wt_collect_top_processes_by_memory(WT_ProcessInfo *out,
                                             size_t limit,
                                             size_t *out_count)
{
    return wt_collect_top_processes(out, limit, WT_PROCESS_SORT_MEMORY, 0,
                                  out_count);
}

void wt_sort_processes_by_memory(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL || count < 2) {
        return;
    }
    qsort(items, count, sizeof(WT_ProcessInfo), wt_compare_by_memory_desc);
}

void wt_sort_processes_by_cpu(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL || count < 2) {
        return;
    }
    qsort(items, count, sizeof(WT_ProcessInfo), wt_compare_by_cpu_desc);
}

void wt_sort_processes_by_disk(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL || count < 2) {
        return;
    }
    qsort(items, count, sizeof(WT_ProcessInfo), wt_compare_by_disk_desc);
}

void wt_sort_processes(WT_ProcessInfo *items, size_t count, WT_ProcessSort sort)
{
    switch (sort) {
    case WT_PROCESS_SORT_CPU:
        wt_sort_processes_by_cpu(items, count);
        break;
    case WT_PROCESS_SORT_DISK:
        wt_sort_processes_by_disk(items, count);
        break;
    case WT_PROCESS_SORT_MEMORY:
    default:
        wt_sort_processes_by_memory(items, count);
        break;
    }
}
