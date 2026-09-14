#include "metrics/disk.h"
#include "metrics/pdh_utils.h"

#include <windows.h>
#include <pdh.h>
#include <strsafe.h>
#include <stdlib.h>

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
            continue;
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
        v->active_percent = -1.0;
        v->read_bytes_per_sec = -1.0;
        v->write_bytes_per_sec = -1.0;
        v->avg_queue_length = -1.0;
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

static int wt_disk_volume_letter(const WT_DiskVolumeMetrics *v, wchar_t *letter_out)
{
    if (v == NULL || letter_out == NULL || v->root_path[0] == L'\0') {
        return 0;
    }
    wchar_t c = v->root_path[0];
    if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) {
        *letter_out = c;
        return 1;
    }
    return 0;
}

typedef struct WT_VolCounters {
    PDH_HCOUNTER active;
    PDH_HCOUNTER read;
    PDH_HCOUNTER write;
    PDH_HCOUNTER queue;
} WT_VolCounters;

static void wt_disk_apply_total(WT_DiskIoMetrics *out,
                                PDH_HCOUNTER c_active,
                                PDH_HCOUNTER c_read,
                                PDH_HCOUNTER c_write,
                                PDH_HCOUNTER c_queue)
{
    int ok_active = 0, ok_read = 0, ok_write = 0, ok_queue = 0;
    double active = wt_pdh_read_double(c_active, &ok_active);
    double read_bps = wt_pdh_read_double(c_read, &ok_read);
    double write_bps = wt_pdh_read_double(c_write, &ok_write);
    double queue = wt_pdh_read_double(c_queue, &ok_queue);

    if (ok_active) {
        out->active_percent = active < 0.0 ? 0.0 : active;
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
}

static void wt_disk_apply_volume(WT_DiskVolumeMetrics *v, const WT_VolCounters *s)
{
    int ok_a = 0, ok_r = 0, ok_w = 0, ok_q = 0;
    double a = wt_pdh_read_double(s->active, &ok_a);
    double r = wt_pdh_read_double(s->read, &ok_r);
    double w = wt_pdh_read_double(s->write, &ok_w);
    double q = wt_pdh_read_double(s->queue, &ok_q);

    if (ok_a) {
        v->active_percent = a < 0.0 ? 0.0 : a;
        v->activity_ok = 1;
    }
    if (ok_r || ok_w) {
        v->read_bytes_per_sec = ok_r ? (r < 0.0 ? 0.0 : r) : 0.0;
        v->write_bytes_per_sec = ok_w ? (w < 0.0 ? 0.0 : w) : 0.0;
        v->throughput_ok = 1;
    }
    if (ok_q) {
        v->avg_queue_length = q < 0.0 ? 0.0 : q;
        v->queue_ok = 1;
    }
}

WT_Result wt_collect_disk_io_ex(unsigned int sample_ms,
                                WT_DiskIoMetrics *total_out,
                                WT_DiskVolumeMetrics *volumes,
                                size_t volume_count)
{
    if (total_out == NULL && (volumes == NULL || volume_count == 0)) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    if (total_out != NULL) {
        ZeroMemory(total_out, sizeof(*total_out));
        total_out->avg_queue_length = -1.0;
    }

    if (sample_ms == 0) {
        sample_ms = WT_DISK_DEFAULT_SAMPLE_MS;
    }

    WT_VolCounters *slots = NULL;
    if (volumes != NULL && volume_count > 0) {
        slots = (WT_VolCounters *)calloc(volume_count, sizeof(WT_VolCounters));
        if (slots == NULL) {
            return WT_ERR_OUT_OF_MEMORY;
        }
    }

    PDH_HQUERY query = NULL;
    PDH_HCOUNTER c_active = NULL;
    PDH_HCOUNTER c_read = NULL;
    PDH_HCOUNTER c_write = NULL;
    PDH_HCOUNTER c_queue = NULL;
    WT_Result result = WT_ERR_PDH;
    size_t vol_added = 0;

    if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
        free(slots);
        return WT_ERR_PDH;
    }

    if (total_out != NULL) {
        (void)PdhAddEnglishCounterW(query,
                                    L"\\PhysicalDisk(_Total)\\% Disk Time", 0,
                                    &c_active);
        (void)PdhAddEnglishCounterW(
            query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &c_read);
        (void)PdhAddEnglishCounterW(
            query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &c_write);
        (void)PdhAddEnglishCounterW(
            query, L"\\PhysicalDisk(_Total)\\Avg. Disk Queue Length", 0,
            &c_queue);
    }

    if (slots != NULL) {
        for (size_t i = 0; i < volume_count; ++i) {
            wchar_t letter = 0;
            if (!wt_disk_volume_letter(&volumes[i], &letter)) {
                continue;
            }
            wchar_t path[96];
            if (SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path),
                                           L"\\LogicalDisk(%c:)\\%% Disk Time",
                                           letter))) {
                (void)PdhAddEnglishCounterW(query, path, 0, &slots[i].active);
            }
            if (SUCCEEDED(StringCchPrintfW(
                    path, ARRAYSIZE(path),
                    L"\\LogicalDisk(%c:)\\Disk Read Bytes/sec", letter))) {
                (void)PdhAddEnglishCounterW(query, path, 0, &slots[i].read);
            }
            if (SUCCEEDED(StringCchPrintfW(
                    path, ARRAYSIZE(path),
                    L"\\LogicalDisk(%c:)\\Disk Write Bytes/sec", letter))) {
                (void)PdhAddEnglishCounterW(query, path, 0, &slots[i].write);
            }
            if (SUCCEEDED(StringCchPrintfW(
                    path, ARRAYSIZE(path),
                    L"\\LogicalDisk(%c:)\\Avg. Disk Queue Length", letter))) {
                (void)PdhAddEnglishCounterW(query, path, 0, &slots[i].queue);
            }
            if (slots[i].active != NULL || slots[i].read != NULL ||
                slots[i].write != NULL) {
                vol_added++;
            }
        }
    }

    if (c_active == NULL && c_read == NULL && c_write == NULL && vol_added == 0) {
        goto cleanup;
    }

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    Sleep(sample_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }

    if (total_out != NULL) {
        wt_disk_apply_total(total_out, c_active, c_read, c_write, c_queue);
    }
    if (slots != NULL) {
        for (size_t i = 0; i < volume_count; ++i) {
            wt_disk_apply_volume(&volumes[i], &slots[i]);
        }
    }

    if ((total_out != NULL &&
         (total_out->active_ok || total_out->throughput_ok ||
          total_out->queue_ok)) ||
        vol_added > 0) {
        result = WT_OK;
    }

cleanup:
    if (query != NULL) {
        PdhCloseQuery(query);
    }
    free(slots);
    return result;
}

WT_Result wt_collect_disk_io(unsigned int sample_ms, WT_DiskIoMetrics *out)
{
    return wt_collect_disk_io_ex(sample_ms, out, NULL, 0);
}

WT_Result wt_enrich_disk_volume_io(WT_DiskVolumeMetrics *volumes,
                                   size_t count,
                                   unsigned int sample_ms)
{
    if (volumes == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return WT_OK;
    }
    return wt_collect_disk_io_ex(sample_ms, NULL, volumes, count);
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
