#include "metrics/disk.h"
#include "metrics/pdh_utils.h"

#include <windows.h>
#include <strsafe.h>

#define WT_DISK_DEFAULT_SAMPLE_MS 500

WT_Result wt_collect_disk_volumes(WT_DiskVolumeMetrics *out,
                                  size_t capacity,
                                  size_t *out_count)
{
    if (out == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    DWORD mask = GetLogicalDrives();
    if (mask == 0) {
        return WT_ERR_WIN32;
    }

    for (int i = 0; i < 26 && *out_count < capacity; ++i) {
        if ((mask & (1UL << i)) == 0) {
            continue;
        }

        wchar_t root[8];
        if (FAILED(StringCchPrintfW(root, ARRAYSIZE(root), L"%c:\\",
                                    (wchar_t)(L'A' + i)))) {
            continue;
        }

        if (GetDriveTypeW(root) != DRIVE_FIXED) {
            continue;
        }

        ULARGE_INTEGER free_to_caller, total, total_free;
        if (!GetDiskFreeSpaceExW(root, &free_to_caller, &total, &total_free)) {
            continue; /* skip volumes we cannot query (e.g. not ready) */
        }

        WT_DiskVolumeMetrics *v = &out[*out_count];
        ZeroMemory(v, sizeof(*v));
        StringCchCopyW(v->root_path, ARRAYSIZE(v->root_path), root);
        v->total_bytes = total.QuadPart;
        v->free_bytes = total_free.QuadPart;
        v->free_percent =
            (total.QuadPart > 0)
                ? ((double)total_free.QuadPart * 100.0 / (double)total.QuadPart)
                : 0.0;
        (*out_count)++;
    }

    return WT_OK;
}

WT_Result wt_collect_disk_activity(unsigned int sample_ms, double *out_percent)
{
    if (out_percent == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_percent = 0.0;

    double value = 0.0;
    WT_Result r = wt_pdh_sample_single(L"\\PhysicalDisk(_Total)\\% Disk Time",
                                       sample_ms ? sample_ms : WT_DISK_DEFAULT_SAMPLE_MS,
                                       &value);
    if (r != WT_OK) {
        return r;
    }

    if (value < 0.0) {
        value = 0.0;
    }
    *out_percent = value;
    return WT_OK;
}
