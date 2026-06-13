#include "metrics/pdh_utils.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <strsafe.h>
#include <stdlib.h>

#define WT_PDH_PROC_MAX_INSTANCES 256

WT_Result wt_pdh_sample_single(const wchar_t *counter_path,
                               unsigned int interval_ms,
                               double *out_value)
{
    if (counter_path == NULL || out_value == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_value = 0.0;

    PDH_HQUERY query = NULL;
    PDH_HCOUNTER counter = NULL;
    WT_Result result = WT_ERR_PDH;

    if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
        return WT_ERR_PDH;
    }

    /* English counter path so this works on localized Windows installs. */
    if (PdhAddEnglishCounterW(query, counter_path, 0, &counter) != ERROR_SUCCESS) {
        goto cleanup;
    }

    /* Rate counters need a baseline collection, a wait, then a second one. */
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    Sleep(interval_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }

    PDH_FMT_COUNTERVALUE value;
    DWORD value_type = 0;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &value_type, &value)
            != ERROR_SUCCESS) {
        goto cleanup;
    }

    *out_value = value.doubleValue;
    result = WT_OK;

cleanup:
    if (query != NULL) {
        PdhCloseQuery(query);
    }
    return result;
}

typedef struct WT_PdhProcSlot {
    PDH_HCOUNTER counter_cpu;
    PDH_HCOUNTER counter_pid;
} WT_PdhProcSlot;

WT_Result wt_pdh_collect_process_cpu(unsigned int interval_ms,
                                     unsigned long *pids,
                                     double *cpu_percent,
                                     size_t capacity,
                                     size_t *out_count)
{
    if (pids == NULL || cpu_percent == NULL || out_count == NULL ||
            capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    if (interval_ms == 0) {
        interval_ms = 500;
    }

    DWORD inst_chars = 0;
    DWORD counter_chars = 0;
    PDH_STATUS st = PdhEnumObjectItemsW(
        NULL, NULL, L"Process", NULL, &inst_chars, NULL, &counter_chars,
        PERF_DETAIL_WIZARD, 0);
    if (st != PDH_MORE_DATA && st != ERROR_SUCCESS) {
        return WT_ERR_PDH;
    }

    wchar_t *inst_buf =
        (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                             (SIZE_T)inst_chars * sizeof(wchar_t));
    if (inst_buf == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    st = PdhEnumObjectItemsW(NULL, NULL, L"Process", inst_buf, &inst_chars,
                             NULL, &counter_chars, PERF_DETAIL_WIZARD, 0);
    if (st != ERROR_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, inst_buf);
        return WT_ERR_PDH;
    }

    PDH_HQUERY query = NULL;
    WT_PdhProcSlot *slots = (WT_PdhProcSlot *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY,
        WT_PDH_PROC_MAX_INSTANCES * sizeof(WT_PdhProcSlot));
    if (slots == NULL) {
        HeapFree(GetProcessHeap(), 0, inst_buf);
        return WT_ERR_OUT_OF_MEMORY;
    }

    unsigned slot_count = 0;
    for (const wchar_t *inst = inst_buf; inst[0] != L'\0';
         inst += wcslen(inst) + 1) {
        if (slot_count >= WT_PDH_PROC_MAX_INSTANCES) {
            break;
        }
        if (_wcsicmp(inst, L"_Total") == 0 || _wcsicmp(inst, L"Idle") == 0) {
            continue;
        }

        wchar_t path_cpu[512];
        wchar_t path_pid[512];
        StringCchPrintfW(path_cpu, ARRAYSIZE(path_cpu),
                         L"\\Process(%s)\\%% Processor Time", inst);
        StringCchPrintfW(path_pid, ARRAYSIZE(path_pid),
                         L"\\Process(%s)\\ID Process", inst);

        if (query == NULL) {
            if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
                HeapFree(GetProcessHeap(), 0, slots);
                HeapFree(GetProcessHeap(), 0, inst_buf);
                return WT_ERR_PDH;
            }
        }

        WT_PdhProcSlot *slot = &slots[slot_count];
        if (PdhAddEnglishCounterW(query, path_cpu, 0, &slot->counter_cpu) !=
                ERROR_SUCCESS) {
            continue;
        }
        if (PdhAddEnglishCounterW(query, path_pid, 0, &slot->counter_pid) !=
                ERROR_SUCCESS) {
            slot->counter_cpu = NULL;
            continue;
        }
        slot_count++;
    }

    HeapFree(GetProcessHeap(), 0, inst_buf);

    if (query == NULL || slot_count == 0) {
        if (query != NULL) {
            PdhCloseQuery(query);
        }
        HeapFree(GetProcessHeap(), 0, slots);
        return WT_ERR_NOT_FOUND;
    }

    WT_Result result = WT_ERR_PDH;
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto proc_cpu_cleanup;
    }
    Sleep(interval_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto proc_cpu_cleanup;
    }

    for (unsigned i = 0; i < slot_count && *out_count < capacity; ++i) {
        PDH_FMT_COUNTERVALUE pid_val;
        PDH_FMT_COUNTERVALUE cpu_val;
        DWORD type = 0;
        if (PdhGetFormattedCounterValue(slots[i].counter_pid, PDH_FMT_LONG,
                                        &type, &pid_val) != ERROR_SUCCESS) {
            continue;
        }
        if (PdhGetFormattedCounterValue(slots[i].counter_cpu, PDH_FMT_DOUBLE,
                                        &type, &cpu_val) != ERROR_SUCCESS) {
            continue;
        }
        if (pid_val.longValue <= 0) {
            continue;
        }

        double cpu = cpu_val.doubleValue;
        if (cpu < 0.0) {
            cpu = 0.0;
        }

        pids[*out_count] = (unsigned long)pid_val.longValue;
        cpu_percent[*out_count] = cpu;
        (*out_count)++;
    }

    result = (*out_count > 0) ? WT_OK : WT_ERR_NOT_FOUND;

proc_cpu_cleanup:
    if (query != NULL) {
        PdhCloseQuery(query);
    }
    HeapFree(GetProcessHeap(), 0, slots);
    return result;
}
