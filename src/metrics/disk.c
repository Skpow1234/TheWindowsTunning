#include "metrics/disk.h"
#include "metrics/pdh_utils.h"

#include <windows.h>
#include <pdh.h>
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

static double wt_pdh_read_double(PDH_HCOUNTER counter, int *ok)
{
    if (ok != NULL) {
        *ok = 0;
    }
    if (counter == NULL) {
        return 0.0;
    }
    PDH_FMT_COUNTERVALUE value;
    DWORD value_type = 0;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &value_type, &value)
            != ERROR_SUCCESS) {
        return 0.0;
    }
    if (ok != NULL) {
        *ok = 1;
    }
    return value.doubleValue;
}

WT_Result wt_collect_disk_io(unsigned int sample_ms, WT_DiskIoMetrics *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    out->avg_queue_length = -1.0;

    if (sample_ms == 0) {
        sample_ms = WT_DISK_DEFAULT_SAMPLE_MS;
    }

    PDH_HQUERY query = NULL;
    PDH_HCOUNTER c_active = NULL;
    PDH_HCOUNTER c_read = NULL;
    PDH_HCOUNTER c_write = NULL;
    PDH_HCOUNTER c_queue = NULL;
    WT_Result result = WT_ERR_PDH;

    if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
        return WT_ERR_PDH;
    }

    /* English paths so localized Windows installs still resolve. */
    (void)PdhAddEnglishCounterW(query,
                                L"\\PhysicalDisk(_Total)\\% Disk Time", 0,
                                &c_active);
    (void)PdhAddEnglishCounterW(query,
                                L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
                                0, &c_read);
    (void)PdhAddEnglishCounterW(query,
                                L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
                                0, &c_write);
    (void)PdhAddEnglishCounterW(
        query, L"\\PhysicalDisk(_Total)\\Avg. Disk Queue Length", 0, &c_queue);

    if (c_active == NULL && c_read == NULL && c_write == NULL) {
        goto cleanup;
    }

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    Sleep(sample_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }

    int ok_active = 0;
    int ok_read = 0;
    int ok_write = 0;
    int ok_queue = 0;
    double active = wt_pdh_read_double(c_active, &ok_active);
    double read_bps = wt_pdh_read_double(c_read, &ok_read);
    double write_bps = wt_pdh_read_double(c_write, &ok_write);
    double queue = wt_pdh_read_double(c_queue, &ok_queue);

    if (ok_active) {
        if (active < 0.0) {
            active = 0.0;
        }
        out->active_percent = active;
        out->active_ok = 1;
    }
    if (ok_read || ok_write) {
        out->read_bytes_per_sec = ok_read ? (read_bps < 0.0 ? 0.0 : read_bps) : 0.0;
        out->write_bytes_per_sec =
            ok_write ? (write_bps < 0.0 ? 0.0 : write_bps) : 0.0;
        out->throughput_ok = 1;
    }
    if (ok_queue) {
        out->avg_queue_length = queue < 0.0 ? 0.0 : queue;
        out->queue_ok = 1;
    }

    if (out->active_ok || out->throughput_ok || out->queue_ok) {
        result = WT_OK;
    }

cleanup:
    if (query != NULL) {
        PdhCloseQuery(query);
    }
    return result;
}

WT_Result wt_collect_disk_activity(unsigned int sample_ms, double *out_percent)
{
    if (out_percent == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_percent = 0.0;

    WT_DiskIoMetrics io;
    WT_Result r = wt_collect_disk_io(sample_ms, &io);
    if (r != WT_OK || !io.active_ok) {
        /* Fall back to single-counter sample for callers that only need %. */
        double value = 0.0;
        r = wt_pdh_sample_single(L"\\PhysicalDisk(_Total)\\% Disk Time",
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

    *out_percent = io.active_percent;
    return WT_OK;
}
