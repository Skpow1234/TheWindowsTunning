#include "metrics/cpu.h"
#include "metrics/pdh_utils.h"

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <pdh.h>
#include <pdhmsg.h>

#define WT_CPU_DEFAULT_SAMPLE_MS 500
#define WT_CPU_HIGH_PCT 50.0
#define WT_CPU_USER_QUIET_PCT 15.0
#define WT_CPU_USER_SUM_PCT 25.0
#define WT_CPU_KERNEL_SHARE_PCT 10.0
#define WT_CPU_KERNEL_TRACE_MS 300
#define WT_CPU_IMAGE_CAP 256
#define WT_CPU_SOURCE_MIN_EVENTS 5

static const GUID WT_SYSTEM_TRACE_GUID =
    {0x9e814aad, 0x3204, 0x11d2, {0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39}};

typedef struct WT_KernelImage {
    unsigned long long base;
    unsigned long long size;
    unsigned int dpc_count;
    unsigned int isr_count;
    char name[64];
} WT_KernelImage;

typedef struct WT_KernelTrace {
    WT_KernelImage images[WT_CPU_IMAGE_CAP];
    int image_count;
    TRACEHANDLE session;
    TRACEHANDLE open;
    EVENT_TRACE_PROPERTIES *prop;
    wchar_t name[64];
    int started;
    HANDLE thread;
} WT_KernelTrace;

static double wt_cpu_clamp(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 100.0) {
        return 100.0;
    }
    return value;
}

static PDH_HQUERY g_cpu_query = NULL;
static PDH_HCOUNTER g_cpu_total = NULL;
static PDH_HCOUNTER g_cpu_dpc = NULL;
static PDH_HCOUNTER g_cpu_interrupt = NULL;
static unsigned g_cpu_gen = 0;

static void wt_cpu_pdh_clear(void)
{
    g_cpu_query = NULL;
    g_cpu_total = NULL;
    g_cpu_dpc = NULL;
    g_cpu_interrupt = NULL;
    g_cpu_gen = 0;
}

static int wt_cpu_read_counter(PDH_HCOUNTER counter, double *out)
{
    PDH_FMT_COUNTERVALUE value;
    DWORD value_type = 0;

    if (counter == NULL || out == NULL) {
        return 0;
    }
    memset(&value, 0, sizeof(value));
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &value_type,
                                    &value) != ERROR_SUCCESS) {
        return 0;
    }
    if (value.CStatus != PDH_CSTATUS_VALID_DATA &&
        value.CStatus != PDH_CSTATUS_NEW_DATA) {
        return 0;
    }
    *out = wt_cpu_clamp(value.doubleValue);
    return 1;
}

WT_Result wt_collect_cpu_metrics(unsigned int sample_ms, WT_CpuMetrics *out)
{
    PDH_HQUERY query = NULL;
    PDH_HCOUNTER total = NULL;
    PDH_HCOUNTER dpc = NULL;
    PDH_HCOUNTER interrupt = NULL;
    double usage = 0.0;
    SYSTEM_INFO info;

    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    out->total_usage_percent = -1.0;
    out->available = 0;
    out->dpc_percent = -1.0;
    out->interrupt_percent = -1.0;
    out->kernel_source_ok = 0;
    out->kernel_source_is_dpc = 0;
    out->kernel_source[0] = '\0';

    GetSystemInfo(&info);
    out->logical_processor_count = info.dwNumberOfProcessors;

    if (sample_ms == 0) {
        sample_ms = WT_CPU_DEFAULT_SAMPLE_MS;
    }
    if (wt_pdh_cache_active() && g_cpu_query != NULL &&
        g_cpu_gen == wt_pdh_cache_generation() && g_cpu_total != NULL) {
        query = g_cpu_query;
        total = g_cpu_total;
        dpc = g_cpu_dpc;
        interrupt = g_cpu_interrupt;
    } else {
        if (g_cpu_query != NULL && wt_pdh_cache_active()) {
            wt_pdh_cache_forget(g_cpu_query);
            wt_cpu_pdh_clear();
        }
        if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
            return WT_ERR_PDH;
        }
        if (PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% Processor Time",
                                  0, &total) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            return WT_ERR_PDH;
        }
        if (PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% DPC Time", 0,
                                  &dpc) != ERROR_SUCCESS) {
            dpc = NULL;
        }
        if (PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% Interrupt Time",
                                  0, &interrupt) != ERROR_SUCCESS) {
            interrupt = NULL;
        }
        if (wt_pdh_cache_track(query)) {
            g_cpu_query = query;
            g_cpu_total = total;
            g_cpu_dpc = dpc;
            g_cpu_interrupt = interrupt;
            g_cpu_gen = wt_pdh_cache_generation();
        }
    }
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        if (g_cpu_query == query) {
            wt_pdh_cache_forget(query);
            wt_cpu_pdh_clear();
        } else {
            PdhCloseQuery(query);
        }
        return WT_ERR_PDH;
    }
    Sleep(sample_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS ||
        !wt_cpu_read_counter(total, &usage)) {
        if (g_cpu_query == query) {
            wt_pdh_cache_forget(query);
            wt_cpu_pdh_clear();
        } else {
            PdhCloseQuery(query);
        }
        return WT_ERR_PDH;
    }
    out->total_usage_percent = usage;
    out->available = 1;
    if (dpc != NULL) {
        double dpc_pct = 0.0;
        if (wt_cpu_read_counter(dpc, &dpc_pct)) {
            out->dpc_percent = dpc_pct;
        }
    }
    if (interrupt != NULL) {
        double irq_pct = 0.0;
        if (wt_cpu_read_counter(interrupt, &irq_pct)) {
            out->interrupt_percent = irq_pct;
        }
    }
    if (g_cpu_query != query) {
        PdhCloseQuery(query);
    }
    return WT_OK;
}

static int wt_kernel_find_name(const BYTE *data, USHORT len, char *out, size_t cap)
{
    size_t i;

    if (data == NULL || out == NULL || cap == 0 || len < 40) {
        return 0;
    }
    for (i = 32; i + 10 < len; i += 2) {
        const wchar_t *text = (const wchar_t *)(data + i);
        size_t max_chars = (len - i) / sizeof(wchar_t);
        size_t n = 0;
        const wchar_t *base;
        const wchar_t *p;
        int printable = 1;

        if (text[0] != L'\\' && text[0] != L'C' && text[0] != L'c') {
            continue;
        }
        for (n = 0; n < max_chars; ++n) {
            if (text[n] == L'\0') {
                break;
            }
            if (text[n] < 32 || text[n] > 126) {
                printable = 0;
                break;
            }
        }
        if (!printable || n < 5 || n >= max_chars || text[n] != L'\0') {
            continue;
        }
        if (wcsstr(text, L".sys") == NULL && wcsstr(text, L".dll") == NULL &&
            wcsstr(text, L".exe") == NULL) {
            continue;
        }
        base = text;
        for (p = text; *p != L'\0'; ++p) {
            if (*p == L'\\' || *p == L'/') {
                base = p + 1;
            }
        }
        if (base[0] == L'\0') {
            continue;
        }
        if (WideCharToMultiByte(CP_UTF8, 0, base, -1, out, (int)cap, NULL,
                                NULL) <= 0) {
            continue;
        }
        return 1;
    }
    return 0;
}

static void wt_kernel_note_image(WT_KernelTrace *trace, const BYTE *data,
                                 USHORT len)
{
    unsigned long long base = 0;
    unsigned long long size = 0;
    char name[64];
    int i;

    if (trace == NULL || trace->image_count >= WT_CPU_IMAGE_CAP) {
        return;
    }
#if defined(_WIN64)
    if (len < 16) {
        return;
    }
    memcpy(&base, data, sizeof(base));
    memcpy(&size, data + sizeof(base), sizeof(size));
    if (base < 0xFFFF000000000000ULL) {
        return;
    }
#else
    if (len < 8) {
        return;
    }
    {
        unsigned long base32 = 0;
        unsigned long size32 = 0;
        memcpy(&base32, data, sizeof(base32));
        memcpy(&size32, data + sizeof(base32), sizeof(size32));
        base = base32;
        size = size32;
    }
    if (base < 0x80000000UL) {
        return;
    }
#endif
    if (size == 0 || size > (128ull << 20)) {
        return;
    }
    if (!wt_kernel_find_name(data, len, name, sizeof(name))) {
        return;
    }
    for (i = 0; i < trace->image_count; ++i) {
        if (trace->images[i].base == base) {
            return;
        }
    }
    trace->images[trace->image_count].base = base;
    trace->images[trace->image_count].size = size;
    trace->images[trace->image_count].dpc_count = 0;
    trace->images[trace->image_count].isr_count = 0;
    StringCchCopyA(trace->images[trace->image_count].name,
                   ARRAYSIZE(trace->images[trace->image_count].name), name);
    trace->image_count++;
}

static void wt_kernel_note_routine(WT_KernelTrace *trace, const BYTE *data,
                                   USHORT len, int is_dpc)
{
    unsigned long long candidates[2];
    int n = 0;
    int c;
    int i;

    if (trace == NULL || data == NULL) {
        return;
    }
#if defined(_WIN64)
    if (len >= 16) {
        memcpy(&candidates[n++], data + 8, sizeof(unsigned long long));
    }
    if (len >= 12) {
        memcpy(&candidates[n++], data + 4, sizeof(unsigned long long));
    }
#else
    if (len >= 8) {
        unsigned long routine = 0;
        memcpy(&routine, data + 4, sizeof(routine));
        candidates[n++] = routine;
    }
#endif
    for (c = 0; c < n; ++c) {
        unsigned long long addr = candidates[c];
        if (addr == 0) {
            continue;
        }
        for (i = 0; i < trace->image_count; ++i) {
            WT_KernelImage *image = &trace->images[i];
            if (addr >= image->base && addr < image->base + image->size) {
                if (is_dpc) {
                    if (image->dpc_count < 0xFFFFFFFFu) {
                        image->dpc_count++;
                    }
                } else if (image->isr_count < 0xFFFFFFFFu) {
                    image->isr_count++;
                }
                return;
            }
        }
    }
}

static VOID WINAPI wt_kernel_on_event(PEVENT_RECORD record)
{
    WT_KernelTrace *trace;
    UCHAR opcode;
    USHORT id;
    const BYTE *data;
    USHORT len;

    if (record == NULL || record->UserContext == NULL) {
        return;
    }
    trace = (WT_KernelTrace *)record->UserContext;
    data = (const BYTE *)record->UserData;
    len = record->UserDataLength;
    if (data == NULL || len == 0) {
        return;
    }
    opcode = record->EventHeader.EventDescriptor.Opcode;
    id = record->EventHeader.EventDescriptor.Id;
    if (opcode == 10 || opcode == 3 || id == 10) {
        wt_kernel_note_image(trace, data, len);
        return;
    }
    if (opcode == 66 || id == 66) {
        wt_kernel_note_routine(trace, data, len, 1);
        return;
    }
    if (opcode == 67 || id == 67) {
        wt_kernel_note_routine(trace, data, len, 0);
    }
}

static DWORD WINAPI wt_kernel_thread(LPVOID param)
{
    WT_KernelTrace *trace = (WT_KernelTrace *)param;
    if (trace != NULL && trace->open != INVALID_PROCESSTRACE_HANDLE) {
        (void)ProcessTrace(&trace->open, 1, NULL, NULL);
    }
    return 0;
}

static void wt_kernel_stop(WT_KernelTrace *trace)
{
    if (trace == NULL) {
        return;
    }
    if (trace->started && trace->prop != NULL) {
        (void)ControlTraceW(trace->session, trace->name, trace->prop,
                            EVENT_TRACE_CONTROL_STOP);
        trace->started = 0;
    }
    if (trace->open != 0 && trace->open != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(trace->open);
        trace->open = 0;
    }
    if (trace->thread != NULL) {
        WaitForSingleObject(trace->thread, 3000);
        CloseHandle(trace->thread);
        trace->thread = NULL;
    }
    if (trace->prop != NULL) {
        free(trace->prop);
        trace->prop = NULL;
    }
}

static int wt_kernel_start(WT_KernelTrace *trace)
{
    size_t name_chars;
    ULONG buffer_size;
    wchar_t *name_slot;
    ULONG rc;
    EVENT_TRACE_LOGFILEW log;

    if (trace == NULL) {
        return 0;
    }
    memset(trace, 0, sizeof(*trace));
    if (FAILED(StringCchPrintfW(trace->name, ARRAYSIZE(trace->name),
                                L"WinTune-DPC-%lu", GetCurrentProcessId()))) {
        return 0;
    }
    name_chars = wcslen(trace->name) + 1;
    buffer_size =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + name_chars * sizeof(wchar_t));
    trace->prop = (EVENT_TRACE_PROPERTIES *)calloc(1, buffer_size);
    if (trace->prop == NULL) {
        return 0;
    }
    trace->prop->Wnode.BufferSize = buffer_size;
    trace->prop->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    trace->prop->Wnode.ClientContext = 1;
    trace->prop->Wnode.Guid = WT_SYSTEM_TRACE_GUID;
    trace->prop->EnableFlags = EVENT_TRACE_FLAG_DPC | EVENT_TRACE_FLAG_INTERRUPT |
                               EVENT_TRACE_FLAG_IMAGE_LOAD;
    trace->prop->LogFileMode =
        EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    trace->prop->BufferSize = 256;
    trace->prop->MinimumBuffers = 8;
    trace->prop->MaximumBuffers = 32;
    trace->prop->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    name_slot = (wchar_t *)((BYTE *)trace->prop + trace->prop->LoggerNameOffset);
    StringCchCopyW(name_slot, name_chars, trace->name);

    rc = StartTraceW(&trace->session, trace->name, trace->prop);
    if (rc == ERROR_ALREADY_EXISTS) {
        (void)ControlTraceW(0, trace->name, trace->prop, EVENT_TRACE_CONTROL_STOP);
        rc = StartTraceW(&trace->session, trace->name, trace->prop);
    }
    if (rc != ERROR_SUCCESS) {
        free(trace->prop);
        trace->prop = NULL;
        return 0;
    }
    trace->started = 1;

    memset(&log, 0, sizeof(log));
    log.LoggerName = trace->name;
    log.ProcessTraceMode =
        PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = wt_kernel_on_event;
    log.Context = trace;
    trace->open = OpenTraceW(&log);
    if (trace->open == INVALID_PROCESSTRACE_HANDLE) {
        wt_kernel_stop(trace);
        return 0;
    }
    trace->thread = CreateThread(NULL, 0, wt_kernel_thread, trace, 0, NULL);
    if (trace->thread == NULL) {
        wt_kernel_stop(trace);
        return 0;
    }
    return 1;
}

static void wt_kernel_pick_source(const WT_KernelTrace *trace, WT_CpuMetrics *cpu)
{
    int best = -1;
    unsigned int best_count = 0;
    int best_is_dpc = 1;
    int i;

    if (trace == NULL || cpu == NULL) {
        return;
    }
    for (i = 0; i < trace->image_count; ++i) {
        if (trace->images[i].dpc_count > best_count) {
            best = i;
            best_count = trace->images[i].dpc_count;
            best_is_dpc = 1;
        }
        if (trace->images[i].isr_count > best_count) {
            best = i;
            best_count = trace->images[i].isr_count;
            best_is_dpc = 0;
        }
    }
    if (best < 0 || best_count < WT_CPU_SOURCE_MIN_EVENTS) {
        return;
    }
    StringCchCopyA(cpu->kernel_source, ARRAYSIZE(cpu->kernel_source),
                   trace->images[best].name);
    cpu->kernel_source_is_dpc = best_is_dpc;
    cpu->kernel_source_ok = 1;
}

void wt_cpu_attribute_kernel_source(WT_CpuMetrics *cpu,
                                    double user_cpu_max,
                                    double user_cpu_sum)
{
    WT_KernelTrace trace;
    double kernel_share;

    if (cpu == NULL || !cpu->available) {
        return;
    }
    cpu->kernel_source_ok = 0;
    cpu->kernel_source_is_dpc = 0;
    cpu->kernel_source[0] = '\0';
    if (cpu->total_usage_percent < WT_CPU_HIGH_PCT) {
        return;
    }
    if (cpu->dpc_percent < 0.0 || cpu->interrupt_percent < 0.0) {
        return;
    }
    kernel_share = cpu->dpc_percent + cpu->interrupt_percent;
    if (kernel_share < WT_CPU_KERNEL_SHARE_PCT) {
        return;
    }
    if (user_cpu_max < 0.0 || user_cpu_sum < 0.0) {
        return;
    }
    if (user_cpu_max >= WT_CPU_USER_QUIET_PCT ||
        user_cpu_sum >= WT_CPU_USER_SUM_PCT) {
        return;
    }
    if (!wt_kernel_start(&trace)) {
        return;
    }
    Sleep(WT_CPU_KERNEL_TRACE_MS);
    wt_kernel_stop(&trace);
    wt_kernel_pick_source(&trace, cpu);
}
