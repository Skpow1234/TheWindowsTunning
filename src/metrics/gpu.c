#include "metrics/gpu.h"

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

#define WT_GPU_DEFAULT_SAMPLE_MS 500
#define WT_GPU_MAX_ENGINE_KEYS   256

#ifndef DXGI_ADAPTER_FLAG_SOFTWARE
#define DXGI_ADAPTER_FLAG_SOFTWARE 2
#endif

typedef struct WT_GpuEngKey {
    unsigned long luid_high;
    unsigned long luid_low;
    wchar_t engtype[64];
    double sum;
} WT_GpuEngKey;

static int wt_gpu_parse_luid(const wchar_t *instance,
                             unsigned long *high,
                             unsigned long *low,
                             wchar_t *engtype,
                             size_t engtype_cap)
{
    if (instance == NULL || high == NULL || low == NULL) {
        return 0;
    }
    *high = 0;
    *low = 0;
    if (engtype != NULL && engtype_cap > 0) {
        engtype[0] = L'\0';
    }

    const wchar_t *p = wcsstr(instance, L"luid_0x");
    if (p == NULL) {
        return 0;
    }
    p += 7;
    wchar_t *end = NULL;
    *high = wcstoul(p, &end, 16);
    if (end == NULL || *end != L'_') {
        return 0;
    }
    if (end[1] != L'0' || end[2] != L'x') {
        return 0;
    }
    *low = wcstoul(end + 3, &end, 16);

    if (engtype != NULL && engtype_cap > 0) {
        const wchar_t *et = wcsstr(instance, L"engtype_");
        if (et != NULL) {
            et += 8;
            StringCchCopyW(engtype, engtype_cap, et);
        }
    }
    return 1;
}

static void wt_gpu_eng_add(WT_GpuEngKey *keys, size_t *count, size_t capacity,
                           unsigned long high, unsigned long low,
                           const wchar_t *engtype, double value)
{
    if (keys == NULL || count == NULL || value < 0.0) {
        return;
    }
    const wchar_t *et = (engtype != NULL) ? engtype : L"";
    for (size_t i = 0; i < *count; ++i) {
        if (keys[i].luid_high == high && keys[i].luid_low == low &&
            _wcsicmp(keys[i].engtype, et) == 0) {
            keys[i].sum += value;
            return;
        }
    }
    if (*count >= capacity) {
        return;
    }
    keys[*count].luid_high = high;
    keys[*count].luid_low = low;
    keys[*count].sum = value;
    StringCchCopyW(keys[*count].engtype, ARRAYSIZE(keys[*count].engtype), et);
    (*count)++;
}

static double wt_gpu_max_for_luid(const WT_GpuEngKey *keys, size_t count,
                                 unsigned long high, unsigned long low)
{
    double best = -1.0;
    for (size_t i = 0; i < count; ++i) {
        if (keys[i].luid_high != high || keys[i].luid_low != low) {
            continue;
        }
        double v = keys[i].sum;
        if (v < 0.0) {
            continue;
        }
        if (v > 100.0) {
            v = 100.0;
        }
        if (v > best) {
            best = v;
        }
    }
    return best;
}

static void wt_gpu_collect_displays(WT_DisplayInfo *out)
{
    ZeroMemory(out, sizeof(*out));
    unsigned int count = 0;
    for (DWORD i = 0; ; ++i) {
        DISPLAY_DEVICEW dd;
        ZeroMemory(&dd, sizeof(dd));
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(NULL, i, &dd, 0)) {
            break;
        }
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0) {
            count++;
        }
    }
    out->display_count = count;
    out->primary_width = (unsigned int)GetSystemMetrics(SM_CXSCREEN);
    out->primary_height = (unsigned int)GetSystemMetrics(SM_CYSCREEN);
    out->available = (count > 0 || out->primary_width > 0) ? 1 : 0;
}

static WT_Result wt_gpu_enum_adapters(WT_GpuMetrics *out)
{
    out->adapter_count = 0;
    out->adapters_ok = 0;

    HRESULT hr_init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int com_owned = 0;
    if (SUCCEEDED(hr_init)) {
        com_owned = 1;
    } else if (hr_init != RPC_E_CHANGED_MODE) {
        return WT_ERR_WIN32;
    }

    IDXGIFactory1 *factory = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr) || factory == NULL) {
        if (com_owned) {
            CoUninitialize();
        }
        return WT_ERR_NOT_SUPPORTED;
    }

    for (UINT i = 0; out->adapter_count < WT_MAX_GPU_ADAPTERS; ++i) {
        IDXGIAdapter1 *adapter = NULL;
        hr = IDXGIFactory1_EnumAdapters1(factory, i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr) || adapter == NULL) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc;
        ZeroMemory(&desc, sizeof(desc));
        if (FAILED(IDXGIAdapter1_GetDesc1(adapter, &desc))) {
            IDXGIAdapter1_Release(adapter);
            continue;
        }
        IDXGIAdapter1_Release(adapter);

        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (wcsstr(desc.Description, L"Microsoft Basic Render") != NULL) {
            continue;
        }

        WT_GpuAdapter *a = &out->adapters[out->adapter_count];
        ZeroMemory(a, sizeof(*a));
        StringCchCopyW(a->name, ARRAYSIZE(a->name), desc.Description);
        a->dedicated_bytes = desc.DedicatedVideoMemory;
        a->shared_bytes = desc.SharedSystemMemory;
        a->luid_high = (unsigned long)desc.AdapterLuid.HighPart;
        a->luid_low = (unsigned long)desc.AdapterLuid.LowPart;
        a->utilization_percent = -1.0;
        a->utilization_ok = 0;
        out->adapter_count++;
    }

    IDXGIFactory1_Release(factory);
    if (com_owned) {
        CoUninitialize();
    }

    out->adapters_ok = (out->adapter_count > 0) ? 1 : 0;
    return out->adapters_ok ? WT_OK : WT_ERR_NOT_FOUND;
}

static WT_Result wt_gpu_sample_utilization(WT_GpuMetrics *out,
                                           unsigned int sample_ms)
{
    if (out == NULL || out->adapter_count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (sample_ms == 0) {
        sample_ms = WT_GPU_DEFAULT_SAMPLE_MS;
    }

    PDH_HQUERY query = NULL;
    PDH_HCOUNTER counter = NULL;
    WT_Result result = WT_ERR_PDH;

    if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
        return WT_ERR_PDH;
    }
    if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage",
                              0, &counter) != ERROR_SUCCESS) {
        goto cleanup;
    }
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    Sleep(sample_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }

    DWORD buf_size = 0;
    DWORD item_count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(
        counter, PDH_FMT_DOUBLE, &buf_size, &item_count, NULL);
    if (st != PDH_MORE_DATA || buf_size == 0) {
        goto cleanup;
    }

    BYTE *buf = (BYTE *)malloc(buf_size);
    if (buf == NULL) {
        result = WT_ERR_OUT_OF_MEMORY;
        goto cleanup;
    }

    PDH_FMT_COUNTERVALUE_ITEM_W *items = (PDH_FMT_COUNTERVALUE_ITEM_W *)buf;
    item_count = 0;
    st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buf_size,
                                      &item_count, items);
    if (st != ERROR_SUCCESS) {
        free(buf);
        goto cleanup;
    }

    WT_GpuEngKey *keys =
        (WT_GpuEngKey *)calloc(WT_GPU_MAX_ENGINE_KEYS, sizeof(WT_GpuEngKey));
    if (keys == NULL) {
        free(buf);
        result = WT_ERR_OUT_OF_MEMORY;
        goto cleanup;
    }
    size_t key_count = 0;

    for (DWORD i = 0; i < item_count; ++i) {
        if (items[i].szName == NULL) {
            continue;
        }
        double v = items[i].FmtValue.doubleValue;
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS || v < 0.0) {
            continue;
        }
        unsigned long hi = 0, lo = 0;
        wchar_t engtype[64];
        if (!wt_gpu_parse_luid(items[i].szName, &hi, &lo, engtype,
                               ARRAYSIZE(engtype))) {
            continue;
        }
        wt_gpu_eng_add(keys, &key_count, WT_GPU_MAX_ENGINE_KEYS, hi, lo,
                       engtype, v);
    }

    out->max_utilization_percent = -1.0;
    out->utilization_ok = 0;
    for (size_t a = 0; a < out->adapter_count; ++a) {
        double u = wt_gpu_max_for_luid(keys, key_count,
                                       out->adapters[a].luid_high,
                                       out->adapters[a].luid_low);
        if (u >= 0.0) {
            out->adapters[a].utilization_percent = u;
            out->adapters[a].utilization_ok = 1;
            out->utilization_ok = 1;
            if (u > out->max_utilization_percent) {
                out->max_utilization_percent = u;
            }
        }
    }

    free(keys);
    free(buf);
    result = out->utilization_ok ? WT_OK : WT_ERR_NOT_FOUND;

cleanup:
    if (query != NULL) {
        PdhCloseQuery(query);
    }
    return result;
}

WT_Result wt_collect_gpu_metrics(unsigned int sample_ms, WT_GpuMetrics *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    out->max_utilization_percent = -1.0;

    wt_gpu_collect_displays(&out->display);

    WT_Result r = wt_gpu_enum_adapters(out);
    if (r != WT_OK) {
        return out->display.available ? WT_OK : r;
    }

    (void)wt_gpu_sample_utilization(out, sample_ms);
    return WT_OK;
}
