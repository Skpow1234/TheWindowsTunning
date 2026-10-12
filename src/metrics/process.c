#include "metrics/process.h"

#include "metrics/pdh_utils.h"
#include "metrics/network.h"
#include "system/file_identity.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <wct.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

#define WT_PROCESS_DEFAULT_SAMPLE_MS 500
#define WT_PROCESS_NET_RATE_CAP 512
#define WT_WAIT_THREADS_PER_PROCESS 6
#define WT_WAIT_QUERY_CAP 96
#define WT_SHORT_POLL_MS 50
#define WT_SHORT_POLL_MAX 20
#define WT_SEEN_CAP 1024

typedef struct WT_ProcessIoSample {
    unsigned long pid;
    unsigned long long read_bytes;
    unsigned long long write_bytes;
} WT_ProcessIoSample;

typedef struct WT_ProcessCpuSample {
    unsigned long pid;
    unsigned long long cpu_time_100ns;
} WT_ProcessCpuSample;

static unsigned long long wt_filetime_to_ull(const FILETIME *ft)
{
    return (((unsigned long long)ft->dwHighDateTime) << 32) |
           (unsigned long long)ft->dwLowDateTime;
}

static WT_Result wt_read_process_cpu_time(unsigned long pid,
                                          unsigned long long *out_time)
{
    if (out_time == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_time = 0;

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == NULL) {
        return WT_ERR_ACCESS_DENIED;
    }

    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    WT_Result result = WT_ERR_WIN32;
    if (GetProcessTimes(proc, &created, &exited, &kernel, &user)) {
        *out_time = wt_filetime_to_ull(&kernel) + wt_filetime_to_ull(&user);
        result = WT_OK;
    }

    CloseHandle(proc);
    return result;
}

static WT_Result wt_sample_process_cpu(WT_ProcessCpuSample *samples,
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
            unsigned long long cpu_time = 0;
            if (wt_read_process_cpu_time(entry.th32ProcessID, &cpu_time) == WT_OK) {
                WT_ProcessCpuSample *s = &samples[*sample_count];
                s->pid = entry.th32ProcessID;
                s->cpu_time_100ns = cpu_time;
                (*sample_count)++;
            }
        } while (Process32NextW(snapshot, &entry));
    } else {
        result = WT_ERR_WIN32;
    }

    CloseHandle(snapshot);
    return result;
}

static const WT_ProcessCpuSample *wt_find_cpu_sample(
    const WT_ProcessCpuSample *samples, size_t count, unsigned long pid)
{
    for (size_t i = 0; i < count; ++i) {
        if (samples[i].pid == pid) {
            return &samples[i];
        }
    }
    return NULL;
}

static void wt_process_init_metrics(WT_ProcessInfo *p)
{
    p->cpu_percent = -1.0;
    p->disk_read_bytes_per_sec = -1.0;
    p->disk_write_bytes_per_sec = -1.0;
    p->net_recv_bytes_per_sec = -1.0;
    p->net_send_bytes_per_sec = -1.0;
    p->udp_recv_bytes_per_sec = -1.0;
    p->udp_send_bytes_per_sec = -1.0;
}

static WT_IntegrityLevel wt_integrity_from_rid(DWORD rid)
{
    if (rid >= SECURITY_MANDATORY_PROTECTED_PROCESS_RID) {
        return WT_INTEGRITY_PROTECTED;
    }
    if (rid >= SECURITY_MANDATORY_SYSTEM_RID) {
        return WT_INTEGRITY_SYSTEM;
    }
    if (rid >= SECURITY_MANDATORY_HIGH_RID) {
        return WT_INTEGRITY_HIGH;
    }
    if (rid >= SECURITY_MANDATORY_MEDIUM_PLUS_RID) {
        return WT_INTEGRITY_MEDIUM_PLUS;
    }
    if (rid >= SECURITY_MANDATORY_MEDIUM_RID) {
        return WT_INTEGRITY_MEDIUM;
    }
    if (rid >= SECURITY_MANDATORY_LOW_RID) {
        return WT_INTEGRITY_LOW;
    }
    return WT_INTEGRITY_UNTRUSTED;
}

static void wt_process_fill_integrity(WT_ProcessInfo *p, HANDLE proc)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &token)) {
        return;
    }

    BYTE buffer[256];
    DWORD len = 0;
    if (!GetTokenInformation(token, TokenIntegrityLevel, buffer, sizeof(buffer),
                             &len)) {
        CloseHandle(token);
        return;
    }

    TOKEN_MANDATORY_LABEL *label = (TOKEN_MANDATORY_LABEL *)buffer;
    PSID sid = label->Label.Sid;
    if (sid == NULL || !IsValidSid(sid)) {
        CloseHandle(token);
        return;
    }
    PUCHAR count = GetSidSubAuthorityCount(sid);
    if (count == NULL || *count == 0) {
        CloseHandle(token);
        return;
    }
    PDWORD rid = GetSidSubAuthority(sid, (DWORD)(*count - 1));
    if (rid == NULL) {
        CloseHandle(token);
        return;
    }
    p->integrity = wt_integrity_from_rid(*rid);
    CloseHandle(token);
}

static void wt_process_fill(WT_ProcessInfo *p, const PROCESSENTRY32W *entry)
{
    ZeroMemory(p, sizeof(*p));
    wt_process_init_metrics(p);
    p->pid = entry->th32ProcessID;
    p->parent_pid = entry->th32ParentProcessID;
    p->integrity = WT_INTEGRITY_UNKNOWN;
    StringCchCopyW(p->name, ARRAYSIZE(p->name), entry->szExeFile);

    DWORD session = 0;
    if (ProcessIdToSessionId(entry->th32ProcessID, &session)) {
        p->session_id = session;
        p->session_ok = 1;
    }

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

        wt_process_fill_integrity(p, proc);

        {
            DWORD handles = 0;
            if (GetProcessHandleCount(proc, &handles)) {
                p->handle_ok = 1;
                p->handle_count = handles;
            }
        }
        {
            DWORD gdi = 0;
            int gdi_read = 0;
            SetLastError(0);
            gdi = GetGuiResources(proc, GR_GDIOBJECTS);
            if (gdi != 0 || GetLastError() == ERROR_SUCCESS) {
                gdi_read = 1;
            } else {
                HANDLE query = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE,
                                           entry->th32ProcessID);
                if (query != NULL) {
                    SetLastError(0);
                    gdi = GetGuiResources(query, GR_GDIOBJECTS);
                    if (gdi != 0 || GetLastError() == ERROR_SUCCESS) {
                        gdi_read = 1;
                    }
                    CloseHandle(query);
                }
            }
            if (gdi_read) {
                p->gdi_ok = 1;
                p->gdi_count = gdi;
            }
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
    case WT_PROCESS_SORT_NETWORK: {
        /* Include idle measured processes, but skip inaccessible shells. */
        if (p->net_recv_bytes_per_sec < 0.0 &&
            p->net_send_bytes_per_sec < 0.0 &&
            p->udp_recv_bytes_per_sec < 0.0 &&
            p->udp_send_bytes_per_sec < 0.0) {
            return 0;
        }
        double rate = 0.0;
        if (p->net_recv_bytes_per_sec >= 0.0) {
            rate += p->net_recv_bytes_per_sec;
        }
        if (p->net_send_bytes_per_sec >= 0.0) {
            rate += p->net_send_bytes_per_sec;
        }
        if (p->udp_recv_bytes_per_sec >= 0.0) {
            rate += p->udp_recv_bytes_per_sec;
        }
        if (p->udp_send_bytes_per_sec >= 0.0) {
            rate += p->udp_send_bytes_per_sec;
        }
        if (rate > 0.0) {
            return 1;
        }
        return (p->working_set_bytes > 0) ? 1 : 0;
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
    case WT_PROCESS_SORT_NETWORK: {
        double rate = 0.0;
        if (p->net_recv_bytes_per_sec >= 0.0) {
            rate += p->net_recv_bytes_per_sec;
        }
        if (p->net_send_bytes_per_sec >= 0.0) {
            rate += p->net_send_bytes_per_sec;
        }
        if (p->udp_recv_bytes_per_sec >= 0.0) {
            rate += p->udp_recv_bytes_per_sec;
        }
        if (p->udp_send_bytes_per_sec >= 0.0) {
            rate += p->udp_send_bytes_per_sec;
        }
        /* Active TCP/UDP traffic ranks first; idle falls back to memory. */
        if (rate > 0.0) {
            return (unsigned long long)rate;
        }
        return p->working_set_bytes;
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

WT_Result wt_process_page_bounds(size_t total,
                                 size_t page,
                                 size_t page_size,
                                 size_t *out_offset,
                                 size_t *out_count,
                                 size_t *out_page_count)
{
    if (page == 0 || page_size == 0 || out_offset == NULL ||
            out_count == NULL || out_page_count == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (page_size > WT_PROCESS_PAGE_MAX) {
        page_size = WT_PROCESS_PAGE_MAX;
    }

    size_t pages = 1;
    if (total > 0) {
        pages = (total + page_size - 1) / page_size;
    }
    *out_page_count = pages;
    if (page > pages) {
        *out_offset = 0;
        *out_count = 0;
        return WT_ERR_NOT_FOUND;
    }

    *out_offset = (page - 1) * page_size;
    if (*out_offset >= total) {
        *out_count = 0;
        return WT_OK;
    }
    size_t remain = total - *out_offset;
    *out_count = (remain < page_size) ? remain : page_size;
    return WT_OK;
}

static void wt_apply_one_net_rate(WT_ProcessInfo *item, const WT_ProcessNetRate *nr,
                                  int udp_measured)
{
    if (nr != NULL) {
        item->net_recv_bytes_per_sec = nr->recv_bytes_per_sec;
        item->net_send_bytes_per_sec = nr->send_bytes_per_sec;
        item->udp_recv_bytes_per_sec = nr->udp_recv_bytes_per_sec;
        item->udp_send_bytes_per_sec = nr->udp_send_bytes_per_sec;
    } else {
        item->net_recv_bytes_per_sec = 0.0;
        item->net_send_bytes_per_sec = 0.0;
        item->udp_recv_bytes_per_sec = udp_measured ? 0.0 : -1.0;
        item->udp_send_bytes_per_sec = udp_measured ? 0.0 : -1.0;
    }
}

static void wt_apply_net_rates(WT_ProcessInfo *items, size_t count,
                               const WT_ProcessNetRate *rates, size_t rate_count,
                               int udp_measured)
{
    for (size_t i = 0; i < count; ++i) {
        const WT_ProcessNetRate *nr =
            wt_find_process_net_rate(rates, rate_count, items[i].pid);
        wt_apply_one_net_rate(&items[i], nr, udp_measured);
    }
}

WT_Result wt_collect_process_page(WT_ProcessInfo *out,
                                  size_t out_cap,
                                  size_t page,
                                  size_t page_size,
                                  WT_ProcessSort sort,
                                  unsigned int sample_ms,
                                  int include_network,
                                  size_t *out_count,
                                  WT_ProcessPageInfo *page_info)
{
    if (out == NULL || out_count == NULL || page_info == NULL || out_cap == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    ZeroMemory(page_info, sizeof(*page_info));

    if (page_size == 0 || page_size > WT_PROCESS_PAGE_MAX) {
        page_size = WT_PROCESS_PAGE_MAX;
    }
    if (sort == WT_PROCESS_SORT_NETWORK) {
        include_network = 1;
    }

    WT_ProcessInfo *all = (WT_ProcessInfo *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        WT_PROCESS_ENUM_MAX * sizeof(WT_ProcessInfo));
    if (all == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t total = 0;
    WT_Result result = wt_collect_processes(all, WT_PROCESS_ENUM_MAX, &total);
    if (result != WT_OK) {
        HeapFree(GetProcessHeap(), 0, all);
        return result;
    }
    page_info->truncated = (total >= WT_PROCESS_ENUM_MAX) ? 1 : 0;

    int need_rates = (sort != WT_PROCESS_SORT_MEMORY) || include_network;
    if (need_rates && total > 0) {
        if (sample_ms == 0) {
            sample_ms = WT_PROCESS_DEFAULT_SAMPLE_MS;
        }

        WT_NetTcpSampleSession net_session;
        ZeroMemory(&net_session, sizeof(net_session));
        int net_sampling = 0;
        if (include_network &&
                wt_net_tcp_sample_start(&net_session, sample_ms) == WT_OK) {
            net_sampling = 1;
        }

        WT_Result enrich = wt_enrich_process_metrics(all, total, sample_ms);
        if (enrich != WT_OK) {
            if (net_sampling) {
                wt_net_tcp_sample_abort(&net_session);
            }
            HeapFree(GetProcessHeap(), 0, all);
            return enrich;
        }

        if (net_sampling) {
            WT_ProcessNetRate net_rates[WT_PROCESS_NET_RATE_CAP];
            size_t net_rate_count = 0;
            if (wt_net_tcp_sample_finish(&net_session, net_rates,
                                         WT_PROCESS_NET_RATE_CAP,
                                         &net_rate_count) == WT_OK) {
                wt_apply_net_rates(all, total, net_rates, net_rate_count,
                                   net_session.udp_measured);
            }
        }
    }

    wt_sort_processes(all, total, sort);

    size_t offset = 0;
    size_t slice = 0;
    size_t pages = 0;
    result = wt_process_page_bounds(total, page, page_size, &offset, &slice,
                                    &pages);
    page_info->page = page;
    page_info->page_size = page_size;
    page_info->page_count = pages;
    page_info->total = total;
    if (result != WT_OK) {
        HeapFree(GetProcessHeap(), 0, all);
        return result;
    }

    if (slice > out_cap) {
        slice = out_cap;
    }
    if (slice > 0) {
        memcpy(out, all + offset, slice * sizeof(WT_ProcessInfo));
    }
    *out_count = slice;
    wt_enrich_process_identity(out, slice);
    wt_enrich_process_wait_chain(out, slice);

    HeapFree(GetProcessHeap(), 0, all);
    return WT_OK;
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

static int wt_compare_by_network_desc(const void *a, const void *b)
{
    const WT_ProcessInfo *pa = (const WT_ProcessInfo *)a;
    const WT_ProcessInfo *pb = (const WT_ProcessInfo *)b;
    double da = 0.0;
    double db = 0.0;
    if (pa->net_recv_bytes_per_sec >= 0.0) {
        da += pa->net_recv_bytes_per_sec;
    }
    if (pa->net_send_bytes_per_sec >= 0.0) {
        da += pa->net_send_bytes_per_sec;
    }
    if (pa->udp_recv_bytes_per_sec >= 0.0) {
        da += pa->udp_recv_bytes_per_sec;
    }
    if (pa->udp_send_bytes_per_sec >= 0.0) {
        da += pa->udp_send_bytes_per_sec;
    }
    if (pb->net_recv_bytes_per_sec >= 0.0) {
        db += pb->net_recv_bytes_per_sec;
    }
    if (pb->net_send_bytes_per_sec >= 0.0) {
        db += pb->net_send_bytes_per_sec;
    }
    if (pb->udp_recv_bytes_per_sec >= 0.0) {
        db += pb->udp_recv_bytes_per_sec;
    }
    if (pb->udp_send_bytes_per_sec >= 0.0) {
        db += pb->udp_send_bytes_per_sec;
    }
    if (da < db) return 1;
    if (da > db) return -1;
    if (pa->working_set_bytes < pb->working_set_bytes) return 1;
    if (pa->working_set_bytes > pb->working_set_bytes) return -1;
    return 0;
}

static int wt_wait_chain_is_hung(const WAITCHAIN_NODE_INFO *nodes, DWORD count,
                                BOOL cycle, int *out_cycle)
{
    if (out_cycle != NULL) {
        *out_cycle = 0;
    }
    if (cycle) {
        if (out_cycle != NULL) {
            *out_cycle = 1;
        }
        return 1;
    }
    for (DWORD i = 0; i < count; ++i) {
        /* A thread blocked in SendMessage is the official "not responding"
         * wait chain. Ordinary blocked threads are normal and are ignored. */
        if (nodes[i].ObjectType == WctSendMessageType) {
            return 1;
        }
    }
    return 0;
}

void wt_enrich_process_wait_chain(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL || count == 0) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        items[i].wait_checked = 0;
        items[i].hung = 0;
        items[i].wait_cycle = 0;
    }

    HWCT session = OpenThreadWaitChainSession(0, NULL);
    if (session == NULL) {
        return;
    }

    unsigned long *tids = (unsigned long *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        count * WT_WAIT_THREADS_PER_PROCESS * sizeof(unsigned long));
    int *seen = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                 count * sizeof(int));
    if (tids == NULL || seen == NULL) {
        if (tids != NULL) {
            HeapFree(GetProcessHeap(), 0, tids);
        }
        if (seen != NULL) {
            HeapFree(GetProcessHeap(), 0, seen);
        }
        CloseThreadWaitChainSession(session);
        return;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                for (size_t i = 0; i < count; ++i) {
                    if (items[i].pid < 8 ||
                            items[i].pid != te.th32OwnerProcessID) {
                        continue;
                    }
                    if (seen[i] >= WT_WAIT_THREADS_PER_PROCESS) {
                        break;
                    }
                    tids[(i * WT_WAIT_THREADS_PER_PROCESS) + (size_t)seen[i]] =
                        te.th32ThreadID;
                    seen[i]++;
                    break;
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }

    int queries = 0;
    for (size_t i = 0; i < count && queries < WT_WAIT_QUERY_CAP; ++i) {
        if (items[i].pid < 8 || seen[i] == 0) {
            continue;
        }
        int any_ok = 0;
        for (int t = 0; t < seen[i] && queries < WT_WAIT_QUERY_CAP; ++t) {
            WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT];
            DWORD node_count = WCT_MAX_NODE_COUNT;
            BOOL cycle = FALSE;
            ZeroMemory(nodes, sizeof(nodes));
            queries++;
            if (!GetThreadWaitChain(
                    session, 0,
                    WCT_OUT_OF_PROC_FLAG | WCT_OUT_OF_PROC_CS_FLAG,
                    tids[(i * WT_WAIT_THREADS_PER_PROCESS) + (size_t)t],
                    &node_count, nodes, &cycle)) {
                continue;
            }
            any_ok = 1;
            int is_cycle = 0;
            if (node_count > WCT_MAX_NODE_COUNT) {
                node_count = WCT_MAX_NODE_COUNT;
            }
            if (wt_wait_chain_is_hung(nodes, node_count, cycle, &is_cycle)) {
                items[i].hung = 1;
                if (is_cycle) {
                    items[i].wait_cycle = 1;
                }
                break;
            }
        }
        items[i].wait_checked = any_ok;
    }

    HeapFree(GetProcessHeap(), 0, tids);
    HeapFree(GetProcessHeap(), 0, seen);
    CloseThreadWaitChainSession(session);
}

void wt_enrich_process_identity(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (items[i].pid == 0) {
            continue;
        }
        if (wt_identity_from_pid(items[i].pid, &items[i].identity) != WT_OK) {
            ZeroMemory(&items[i].identity, sizeof(items[i].identity));
            items[i].identity.signature = WT_SIG_UNAVAILABLE;
            items[i].identity.origin = WT_ORIGIN_UNKNOWN;
        }
    }
}

static int wt_process_kernel_pseudo(unsigned long pid, const wchar_t *name)
{
    if (pid <= 4) {
        return 1;
    }
    if (name == NULL) {
        return 1;
    }
    return _wcsicmp(name, L"Idle") == 0 ||
           _wcsicmp(name, L"System") == 0 ||
           _wcsicmp(name, L"System Idle Process") == 0 ||
           _wcsicmp(name, L"Registry") == 0 ||
           _wcsicmp(name, L"Memory Compression") == 0 ||
           _wcsicmp(name, L"Secure System") == 0;
}

typedef struct WT_SeenProc {
    unsigned long pid;
    unsigned long parent_pid;
    wchar_t name[WT_SHORT_LIVED_NAME_MAX];
    int at_start;
    int at_end;
} WT_SeenProc;

static void wt_seen_note(WT_SeenProc *seen, size_t *count,
                         const PROCESSENTRY32W *entry, int at_start, int at_end)
{
    size_t i;

    if (entry == NULL || entry->th32ProcessID <= 4) {
        return;
    }
    for (i = 0; i < *count; ++i) {
        if (seen[i].pid == entry->th32ProcessID) {
            if (at_start) {
                seen[i].at_start = 1;
            }
            if (at_end) {
                seen[i].at_end = 1;
            }
            return;
        }
    }
    if (*count >= WT_SEEN_CAP) {
        return;
    }
    ZeroMemory(&seen[*count], sizeof(seen[*count]));
    seen[*count].pid = entry->th32ProcessID;
    seen[*count].parent_pid = entry->th32ParentProcessID;
    StringCchCopyW(seen[*count].name, WT_SHORT_LIVED_NAME_MAX, entry->szExeFile);
    seen[*count].at_start = at_start ? 1 : 0;
    seen[*count].at_end = at_end ? 1 : 0;
    (*count)++;
}

static void wt_seen_snapshot(WT_SeenProc *seen, size_t *count, int at_start,
                             int at_end)
{
    HANDLE snapshot;
    PROCESSENTRY32W entry;

    if (seen == NULL || count == NULL) {
        return;
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            wt_seen_note(seen, count, &entry, at_start, at_end);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static void wt_seen_emit(const WT_SeenProc *seen, size_t seen_count,
                         WT_ShortLivedProcess *out, size_t cap, size_t *out_count)
{
    int pass;
    size_t i;
    size_t n = 0;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (seen == NULL || out == NULL || out_count == NULL || cap == 0) {
        return;
    }
    if (cap > WT_MAX_SHORT_LIVED) {
        cap = WT_MAX_SHORT_LIVED;
    }
    for (pass = 0; pass < 2 && n < cap; ++pass) {
        for (i = 0; i < seen_count && n < cap; ++i) {
            int born_died = !seen[i].at_start && !seen[i].at_end;
            int exited = seen[i].at_start && !seen[i].at_end;
            if (pass == 0 && !born_died) {
                continue;
            }
            if (pass == 1 && !exited) {
                continue;
            }
            if (wt_process_kernel_pseudo(seen[i].pid, seen[i].name)) {
                continue;
            }
            ZeroMemory(&out[n], sizeof(out[n]));
            out[n].pid = seen[i].pid;
            out[n].parent_pid = seen[i].parent_pid;
            StringCchCopyW(out[n].name, WT_SHORT_LIVED_NAME_MAX, seen[i].name);
            out[n].exited = 1;
            n++;
        }
    }
    *out_count = n;
}

static void wt_sample_window(unsigned int sample_ms, WT_SeenProc *seen,
                             size_t *seen_count)
{
    unsigned int left = sample_ms;
    unsigned int polls = 0;

    if (seen != NULL && seen_count != NULL) {
        wt_seen_snapshot(seen, seen_count, 1, 0);
    }
    while (left > 0) {
        unsigned int slice = WT_SHORT_POLL_MS;
        if (polls >= WT_SHORT_POLL_MAX || slice > left) {
            slice = left;
        }
        Sleep(slice);
        left -= slice;
        if (seen != NULL && seen_count != NULL && left > 0 &&
            polls < WT_SHORT_POLL_MAX) {
            wt_seen_snapshot(seen, seen_count, 0, 0);
            polls++;
        }
    }
}

WT_Result wt_collect_top_processes(WT_ProcessInfo *out,
                                   size_t limit,
                                   WT_ProcessSort sort,
                                   unsigned int sample_ms,
                                   int include_network,
                                   size_t *out_count,
                                   double *out_user_cpu_max,
                                   double *out_user_cpu_sum,
                                   WT_ShortLivedProcess *out_short_lived,
                                   size_t short_lived_cap,
                                   size_t *out_short_lived_count)
{
    double user_max = -1.0;
    double user_sum = 0.0;
    int user_seen = 0;
    SYSTEM_INFO sysinfo;
    double nproc;

    if (out_user_cpu_max != NULL) {
        *out_user_cpu_max = -1.0;
    }
    if (out_user_cpu_sum != NULL) {
        *out_user_cpu_sum = -1.0;
    }
    if (out_short_lived_count != NULL) {
        *out_short_lived_count = 0;
    }
    if (out == NULL || out_count == NULL || limit == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    GetSystemInfo(&sysinfo);
    nproc = (sysinfo.dwNumberOfProcessors > 0)
                ? (double)sysinfo.dwNumberOfProcessors
                : 1.0;

    if (sort == WT_PROCESS_SORT_NETWORK) {
        include_network = 1;
    }

    if (sample_ms == 0 && sort == WT_PROCESS_SORT_MEMORY && !include_network) {
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
            wt_enrich_process_identity(out, *out_count);
            wt_enrich_process_wait_chain(out, *out_count);
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

    WT_ProcessCpuSample *cpu_before = (WT_ProcessCpuSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        4096 * sizeof(WT_ProcessCpuSample));
    if (cpu_before == NULL) {
        HeapFree(GetProcessHeap(), 0, io_before);
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t cpu_before_count = 0;
    (void)wt_sample_process_cpu(cpu_before, &cpu_before_count, 4096);

    WT_NetTcpSampleSession net_session;
    ZeroMemory(&net_session, sizeof(net_session));
    int net_sampling = 0;
    if (include_network) {
        if (wt_net_tcp_sample_start(&net_session, sample_ms) == WT_OK) {
            net_sampling = 1;
        }
    }

    WT_SeenProc *seen = NULL;
    size_t seen_count = 0;
    if (out_short_lived != NULL && out_short_lived_count != NULL &&
        short_lived_cap > 0) {
        seen = (WT_SeenProc *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                        WT_SEEN_CAP * sizeof(WT_SeenProc));
    }
    wt_sample_window(sample_ms, seen, seen != NULL ? &seen_count : NULL);

    WT_ProcessNetRate net_rates[WT_PROCESS_NET_RATE_CAP];
    size_t net_rate_count = 0;
    int net_ok = 0;
    if (net_sampling) {
        if (wt_net_tcp_sample_finish(&net_session, net_rates,
                                     WT_PROCESS_NET_RATE_CAP,
                                     &net_rate_count) == WT_OK) {
            net_ok = 1;
        }
    }

    WT_ProcessCpuSample *cpu_after = (WT_ProcessCpuSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        4096 * sizeof(WT_ProcessCpuSample));
    if (cpu_after == NULL) {
        if (net_sampling) {
            wt_net_tcp_sample_abort(&net_session);
        }
        HeapFree(GetProcessHeap(), 0, cpu_before);
        HeapFree(GetProcessHeap(), 0, io_before);
        if (seen != NULL) {
            HeapFree(GetProcessHeap(), 0, seen);
        }
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t cpu_after_count = 0;
    (void)wt_sample_process_cpu(cpu_after, &cpu_after_count, 4096);

    WT_ProcessIoSample *io_after = (WT_ProcessIoSample *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        4096 * sizeof(WT_ProcessIoSample));
    if (io_after == NULL) {
        HeapFree(GetProcessHeap(), 0, cpu_after);
        HeapFree(GetProcessHeap(), 0, cpu_before);
        HeapFree(GetProcessHeap(), 0, io_before);
        if (seen != NULL) {
            HeapFree(GetProcessHeap(), 0, seen);
        }
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t io_after_count = 0;
    (void)wt_sample_process_io(io_after, &io_after_count, 4096);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, io_after);
        HeapFree(GetProcessHeap(), 0, cpu_after);
        HeapFree(GetProcessHeap(), 0, cpu_before);
        HeapFree(GetProcessHeap(), 0, io_before);
        if (seen != NULL) {
            HeapFree(GetProcessHeap(), 0, seen);
        }
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
            if (seen != NULL) {
                wt_seen_note(seen, &seen_count, &entry, 0, 1);
            }

            if (info.cpu_percent < 0.0) {
                const WT_ProcessCpuSample *cb =
                    wt_find_cpu_sample(cpu_before, cpu_before_count, info.pid);
                const WT_ProcessCpuSample *ca =
                    wt_find_cpu_sample(cpu_after, cpu_after_count, info.pid);
                if (cb != NULL && ca != NULL &&
                        ca->cpu_time_100ns >= cb->cpu_time_100ns &&
                        secs > 0.0) {
                    unsigned long long delta =
                        ca->cpu_time_100ns - cb->cpu_time_100ns;
                    info.cpu_percent =
                        ((double)delta / (secs * 10000000.0)) * 100.0;
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

            if (net_ok) {
                const WT_ProcessNetRate *nr =
                    wt_find_process_net_rate(net_rates, net_rate_count,
                                             info.pid);
                wt_apply_one_net_rate(&info, nr, net_session.udp_measured);
            }

            if (!wt_process_kernel_pseudo(info.pid, info.name) &&
                info.cpu_percent >= 0.0) {
                double machine = info.cpu_percent / nproc;
                if (machine < 0.0) {
                    machine = 0.0;
                }
                user_sum += machine;
                if (!user_seen || machine > user_max) {
                    user_max = machine;
                }
                user_seen = 1;
            }

            wt_topk_insert(out, out_count, limit, &info, sort);
        } while (Process32NextW(snapshot, &entry));
    } else {
        result = WT_ERR_WIN32;
    }

    CloseHandle(snapshot);
    wt_seen_emit(seen, seen_count, out_short_lived, short_lived_cap,
                 out_short_lived_count);
    if (seen != NULL) {
        HeapFree(GetProcessHeap(), 0, seen);
    }
    HeapFree(GetProcessHeap(), 0, io_before);
    HeapFree(GetProcessHeap(), 0, io_after);
    HeapFree(GetProcessHeap(), 0, cpu_before);
    HeapFree(GetProcessHeap(), 0, cpu_after);

    if (result == WT_OK) {
        wt_sort_processes(out, *out_count, sort);
        wt_enrich_process_identity(out, *out_count);
        wt_enrich_process_wait_chain(out, *out_count);
        if (user_seen) {
            if (out_user_cpu_max != NULL) {
                *out_user_cpu_max = user_max;
            }
            if (out_user_cpu_sum != NULL) {
                *out_user_cpu_sum = user_sum;
            }
        }
    }
    return result;
}

WT_Result wt_collect_top_processes_by_memory(WT_ProcessInfo *out,
                                             size_t limit,
                                             size_t *out_count)
{
    return wt_collect_top_processes(out, limit, WT_PROCESS_SORT_MEMORY, 0, 0,
                                    out_count, NULL, NULL, NULL, 0, NULL);
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

void wt_sort_processes_by_network(WT_ProcessInfo *items, size_t count)
{
    if (items == NULL || count < 2) {
        return;
    }
    qsort(items, count, sizeof(WT_ProcessInfo), wt_compare_by_network_desc);
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
    case WT_PROCESS_SORT_NETWORK:
        wt_sort_processes_by_network(items, count);
        break;
    case WT_PROCESS_SORT_MEMORY:
    default:
        wt_sort_processes_by_memory(items, count);
        break;
    }
}
