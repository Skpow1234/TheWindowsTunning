#include "metrics/gpu.h"
#include "metrics/pdh_utils.h"

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <tlhelp32.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>

#define WT_GPU_DEFAULT_SAMPLE_MS 500
#define WT_GPU_MAX_ENGINE_KEYS   256
#define WT_GPU_MAX_PID_KEYS      1024

#ifndef DXGI_ADAPTER_FLAG_SOFTWARE
#define DXGI_ADAPTER_FLAG_SOFTWARE 2
#endif

typedef struct WT_GpuEngKey {
    unsigned long luid_high;
    unsigned long luid_low;
    wchar_t engtype[64];
    double sum;
} WT_GpuEngKey;

typedef struct WT_GpuPidKey {
    unsigned long luid_high;
    unsigned long luid_low;
    unsigned long pid;
    double sum;
} WT_GpuPidKey;

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

static int wt_gpu_parse_pid(const wchar_t *instance, unsigned long *pid)
{
    if (instance == NULL || pid == NULL) {
        return 0;
    }
    const wchar_t *p = wcsstr(instance, L"pid_");
    if (p == NULL) {
        return 0;
    }
    p += 4;
    wchar_t *end = NULL;
    unsigned long value = wcstoul(p, &end, 10);
    if (end == p) {
        return 0;
    }
    *pid = value;
    return 1;
}

static void wt_gpu_pid_add(WT_GpuPidKey *keys, size_t *count, size_t capacity,
                           unsigned long high, unsigned long low,
                           unsigned long pid, double value)
{
    if (keys == NULL || count == NULL || value < 0.0 || pid == 0) {
        return;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (keys[i].luid_high == high && keys[i].luid_low == low &&
            keys[i].pid == pid) {
            keys[i].sum += value;
            return;
        }
    }
    if (*count >= capacity) {
        return;
    }
    keys[*count].luid_high = high;
    keys[*count].luid_low = low;
    keys[*count].pid = pid;
    keys[*count].sum = value;
    (*count)++;
}

static void wt_gpu_keep_process(WT_GpuProcessUse *slots, size_t *count,
                                size_t capacity, unsigned long pid, double sum)
{
    if (slots == NULL || count == NULL || capacity == 0 || sum <= 0.0) {
        return;
    }
    if (sum > 100.0) {
        sum = 100.0;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (slots[i].pid == pid) {
            if (sum > slots[i].engine_percent) {
                slots[i].engine_percent = sum;
            }
            return;
        }
    }

    size_t index = *count;
    if (*count < capacity) {
        (*count)++;
    } else if (sum <= slots[capacity - 1].engine_percent) {
        return;
    } else {
        index = capacity - 1;
    }

    slots[index].pid = pid;
    slots[index].engine_percent = sum;
    slots[index].name[0] = L'\0';
    for (size_t i = index; i > 0 &&
         slots[i].engine_percent > slots[i - 1].engine_percent; --i) {
        WT_GpuProcessUse tmp = slots[i - 1];
        slots[i - 1] = slots[i];
        slots[i] = tmp;
    }
}

static void wt_gpu_resolve_names(WT_GpuMetrics *out)
{
    if (out == NULL) {
        return;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            for (size_t a = 0; a < out->adapter_count; ++a) {
                WT_GpuAdapter *adapter = &out->adapters[a];
                for (size_t p = 0; p < adapter->process_count; ++p) {
                    if (adapter->processes[p].pid == entry.th32ProcessID &&
                        adapter->processes[p].name[0] == L'\0') {
                        StringCchCopyW(adapter->processes[p].name,
                                       ARRAYSIZE(adapter->processes[p].name),
                                       entry.szExeFile);
                    }
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
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

static void wt_display_set_primary_size(WT_DisplayInfo *out)
{
    unsigned int i;
    for (i = 0; i < out->monitor_count; ++i) {
        if (out->monitors[i].primary && out->monitors[i].width > 0) {
            out->primary_width = out->monitors[i].width;
            out->primary_height = out->monitors[i].height;
            return;
        }
    }
    if (out->monitor_count > 0 && out->monitors[0].width > 0) {
        out->primary_width = out->monitors[0].width;
        out->primary_height = out->monitors[0].height;
    }
}

static int wt_display_from_config(WT_DisplayInfo *out)
{
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    DISPLAYCONFIG_PATH_INFO *paths = NULL;
    DISPLAYCONFIG_MODE_INFO *modes = NULL;
    LONG rc;
    UINT32 i;
    int saw_primary = 0;

    rc = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count,
                                     &mode_count);
    if (rc != ERROR_SUCCESS || path_count == 0 || mode_count == 0) {
        return 0;
    }
    if (path_count > 64) {
        path_count = 64;
    }
    if (mode_count > 128) {
        mode_count = 128;
    }
    paths = (DISPLAYCONFIG_PATH_INFO *)calloc(path_count, sizeof(*paths));
    modes = (DISPLAYCONFIG_MODE_INFO *)calloc(mode_count, sizeof(*modes));
    if (paths == NULL || modes == NULL) {
        free(paths);
        free(modes);
        return 0;
    }
    rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths,
                            &mode_count, modes, NULL);
    if (rc != ERROR_SUCCESS) {
        free(paths);
        free(modes);
        return 0;
    }

    for (i = 0; i < path_count && out->monitor_count < WT_MAX_DISPLAYS; ++i) {
        WT_DisplayMonitor *mon;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target;
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color;
        UINT32 mode_index;
        DISPLAYCONFIG_RATIONAL rate;

        if ((paths[i].flags & DISPLAYCONFIG_PATH_ACTIVE) == 0) {
            continue;
        }
        mon = &out->monitors[out->monitor_count];
        ZeroMemory(mon, sizeof(*mon));
        mon->refresh_hz = -1.0;

        mode_index = paths[i].sourceInfo.modeInfoIdx;
        if (mode_index != DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
            mode_index < mode_count &&
            modes[mode_index].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            mon->width = modes[mode_index].sourceMode.width;
            mon->height = modes[mode_index].sourceMode.height;
            if (modes[mode_index].sourceMode.position.x == 0 &&
                modes[mode_index].sourceMode.position.y == 0) {
                mon->primary = 1;
                saw_primary = 1;
            }
        }

        rate = paths[i].targetInfo.refreshRate;
        if (rate.Denominator != 0 && rate.Numerator != 0) {
            double hz = (double)rate.Numerator / (double)rate.Denominator;
            if (hz >= 1.0 && hz <= 1000.0) {
                mon->refresh_hz = hz;
            }
        }

        ZeroMemory(&target, sizeof(target));
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
            target.monitorFriendlyDeviceName[0] != L'\0') {
            StringCchCopyW(mon->name, ARRAYSIZE(mon->name),
                           target.monitorFriendlyDeviceName);
        }

        ZeroMemory(&color, sizeof(color));
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof(color);
        color.header.adapterId = paths[i].targetInfo.adapterId;
        color.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
            mon->hdr_ok = 1;
            mon->hdr_supported = color.advancedColorSupported ? 1 : 0;
            mon->hdr_enabled = color.advancedColorEnabled ? 1 : 0;
        }

        out->monitor_count++;
    }

    free(paths);
    free(modes);
    if (out->monitor_count == 0) {
        return 0;
    }
    if (!saw_primary) {
        out->monitors[0].primary = 1;
    }
    return 1;
}

static void wt_display_from_enum(WT_DisplayInfo *out)
{
    DWORD i;
    for (i = 0; out->monitor_count < WT_MAX_DISPLAYS; ++i) {
        DISPLAY_DEVICEW adapter;
        DISPLAY_DEVICEW monitor;
        DEVMODEW mode;
        WT_DisplayMonitor *mon;

        ZeroMemory(&adapter, sizeof(adapter));
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(NULL, i, &adapter, 0)) {
            break;
        }
        if ((adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
            continue;
        }
        mon = &out->monitors[out->monitor_count];
        ZeroMemory(mon, sizeof(*mon));
        mon->refresh_hz = -1.0;
        mon->primary =
            (adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0 ? 1 : 0;

        ZeroMemory(&monitor, sizeof(monitor));
        monitor.cb = sizeof(monitor);
        if (EnumDisplayDevicesW(adapter.DeviceName, 0, &monitor, 0) &&
            monitor.DeviceString[0] != L'\0') {
            StringCchCopyW(mon->name, ARRAYSIZE(mon->name), monitor.DeviceString);
        } else if (adapter.DeviceString[0] != L'\0') {
            StringCchCopyW(mon->name, ARRAYSIZE(mon->name), adapter.DeviceString);
        }

        ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS,
                                 &mode)) {
            if ((mode.dmFields & DM_PELSWIDTH) != 0) {
                mon->width = (unsigned int)mode.dmPelsWidth;
            }
            if ((mode.dmFields & DM_PELSHEIGHT) != 0) {
                mon->height = (unsigned int)mode.dmPelsHeight;
            }
            if ((mode.dmFields & DM_DISPLAYFREQUENCY) != 0 &&
                mode.dmDisplayFrequency > 1 &&
                mode.dmDisplayFrequency <= 1000) {
                mon->refresh_hz = (double)mode.dmDisplayFrequency;
            }
        }
        out->monitor_count++;
    }
}

static void wt_gpu_collect_displays(WT_DisplayInfo *out)
{
    ZeroMemory(out, sizeof(*out));
    if (!wt_display_from_config(out)) {
        out->monitor_count = 0;
        wt_display_from_enum(out);
    }
    out->display_count = out->monitor_count;
    out->primary_width = (unsigned int)GetSystemMetrics(SM_CXSCREEN);
    out->primary_height = (unsigned int)GetSystemMetrics(SM_CYSCREEN);
    wt_display_set_primary_size(out);
    out->available = (out->display_count > 0 || out->primary_width > 0) ? 1 : 0;
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

static PDH_HQUERY g_gpu_query = NULL;
static PDH_HCOUNTER g_gpu_counter = NULL;
static unsigned g_gpu_gen = 0;

static void wt_gpu_pdh_clear(void)
{
    g_gpu_query = NULL;
    g_gpu_counter = NULL;
    g_gpu_gen = 0;
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
    int cached = 0;
    int query_ok = 0;

    if (wt_pdh_cache_active() && g_gpu_query != NULL && g_gpu_counter != NULL &&
        g_gpu_gen == wt_pdh_cache_generation()) {
        query = g_gpu_query;
        counter = g_gpu_counter;
        cached = 1;
    } else {
        if (g_gpu_query != NULL && wt_pdh_cache_active()) {
            wt_pdh_cache_forget(g_gpu_query);
            wt_gpu_pdh_clear();
        }
        if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
            return WT_ERR_PDH;
        }
        if (PdhAddEnglishCounterW(query,
                                  L"\\GPU Engine(*)\\Utilization Percentage", 0,
                                  &counter) != ERROR_SUCCESS) {
            goto cleanup;
        }
        if (wt_pdh_cache_track(query)) {
            g_gpu_query = query;
            g_gpu_counter = counter;
            g_gpu_gen = wt_pdh_cache_generation();
            cached = 1;
        }
    }
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    Sleep(sample_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    query_ok = 1;

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
    WT_GpuPidKey *pids =
        (WT_GpuPidKey *)calloc(WT_GPU_MAX_PID_KEYS, sizeof(WT_GpuPidKey));
    if (keys == NULL || pids == NULL) {
        free(keys);
        free(pids);
        free(buf);
        result = WT_ERR_OUT_OF_MEMORY;
        goto cleanup;
    }
    size_t key_count = 0;
    size_t pid_count = 0;

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
        unsigned long pid = 0;
        if (wt_gpu_parse_pid(items[i].szName, &pid)) {
            wt_gpu_pid_add(pids, &pid_count, WT_GPU_MAX_PID_KEYS, hi, lo, pid, v);
        }
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
        out->adapters[a].process_count = 0;
        for (size_t p = 0; p < pid_count; ++p) {
            if (pids[p].luid_high != out->adapters[a].luid_high ||
                pids[p].luid_low != out->adapters[a].luid_low) {
                continue;
            }
            wt_gpu_keep_process(out->adapters[a].processes,
                                &out->adapters[a].process_count,
                                WT_GPU_PROCS_PER_ADAPTER, pids[p].pid,
                                pids[p].sum);
        }
    }
    wt_gpu_resolve_names(out);

    free(pids);
    free(keys);
    free(buf);
    result = out->utilization_ok ? WT_OK : WT_ERR_NOT_FOUND;

cleanup:
    if (!query_ok) {
        if (g_gpu_query == query) {
            wt_pdh_cache_forget(query);
            wt_gpu_pdh_clear();
        } else if (query != NULL) {
            PdhCloseQuery(query);
        }
    } else if (!cached && query != NULL) {
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
