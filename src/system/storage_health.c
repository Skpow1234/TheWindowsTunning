#include "system/storage_health.h"

#include <stdio.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <strsafe.h>

const char *wt_storage_health_status_name(WT_StorageHealthStatus status)
{
    switch (status) {
    case WT_STORAGE_HEALTH_OK:          return "ok";
    case WT_STORAGE_HEALTH_WARNING:     return "warning";
    case WT_STORAGE_HEALTH_DEGRADED:    return "degraded";
    case WT_STORAGE_HEALTH_UNAVAILABLE: return "unavailable";
    default:                            return "unknown";
    }
}

void wt_storage_health_init(WT_StorageHealthReport *report)
{
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

static void wt_storage_copy_desc_string(wchar_t *dst, size_t dst_count,
                                        const STORAGE_DEVICE_DESCRIPTOR *desc,
                                        ULONG offset)
{
    const char *src;
    size_t i;

    if (dst == NULL || dst_count == 0) {
        return;
    }
    dst[0] = L'\0';
    if (desc == NULL || offset == 0 || offset >= desc->Size) {
        return;
    }
    src = (const char *)desc + offset;
    for (i = 0; i + 1 < dst_count && src[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x20 || c > 0x7e) {
            dst[i] = L'?';
        } else {
            dst[i] = (wchar_t)c;
        }
    }
    dst[i] = L'\0';
    /* Trim trailing spaces common in ATA strings. */
    while (i > 0 && (dst[i - 1] == L' ' || dst[i - 1] == L'\t')) {
        dst[--i] = L'\0';
    }
}

static void wt_storage_bus_name(STORAGE_BUS_TYPE bus, wchar_t *out, size_t count)
{
    const wchar_t *name = L"unknown";
    switch (bus) {
    case BusTypeScsi:      name = L"scsi"; break;
    case BusTypeAtapi:     name = L"atapi"; break;
    case BusTypeAta:       name = L"ata"; break;
    case BusType1394:      name = L"1394"; break;
    case BusTypeSsa:       name = L"ssa"; break;
    case BusTypeFibre:     name = L"fibre"; break;
    case BusTypeUsb:       name = L"usb"; break;
    case BusTypeRAID:      name = L"raid"; break;
    case BusTypeiScsi:     name = L"iscsi"; break;
    case BusTypeSas:       name = L"sas"; break;
    case BusTypeSata:      name = L"sata"; break;
    case BusTypeSd:        name = L"sd"; break;
    case BusTypeMmc:       name = L"mmc"; break;
    case BusTypeVirtual:   name = L"virtual"; break;
    case BusTypeFileBackedVirtual: name = L"file_virtual"; break;
    case BusTypeSpaces:    name = L"spaces"; break;
    case BusTypeNvme:      name = L"nvme"; break;
    default: break;
    }
    StringCchCopyW(out, count, name);
}

static void wt_storage_query_media_hint(HANDLE h, wchar_t *out, size_t count)
{
    STORAGE_PROPERTY_QUERY q;
    DWORD bytes = 0;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek;
    DEVICE_TRIM_DESCRIPTOR trim;

    StringCchCopyW(out, count, L"unknown");
    memset(&q, 0, sizeof(q));
    q.PropertyId = StorageDeviceSeekPenaltyProperty;
    q.QueryType = PropertyStandardQuery;
    memset(&seek, 0, sizeof(seek));
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                        &seek, sizeof(seek), &bytes, NULL) &&
        bytes >= sizeof(seek)) {
        if (seek.IncursSeekPenalty) {
            StringCchCopyW(out, count, L"hdd");
            return;
        }
        StringCchCopyW(out, count, L"ssd");
        return;
    }

    memset(&q, 0, sizeof(q));
    q.PropertyId = StorageDeviceTrimProperty;
    q.QueryType = PropertyStandardQuery;
    memset(&trim, 0, sizeof(trim));
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                        &trim, sizeof(trim), &bytes, NULL) &&
        bytes >= sizeof(trim) && trim.TrimEnabled) {
        StringCchCopyW(out, count, L"ssd");
    }
}

static WT_Result wt_storage_query_descriptor(HANDLE h, WT_StorageDiskHealth *d)
{
    BYTE buf[1024];
    STORAGE_PROPERTY_QUERY q;
    DWORD bytes = 0;
    STORAGE_DEVICE_DESCRIPTOR *desc;

    memset(&q, 0, sizeof(q));
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    memset(buf, 0, sizeof(buf));

    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                         buf, sizeof(buf), &bytes, NULL) ||
        bytes < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return WT_ERR_WIN32;
    }

    desc = (STORAGE_DEVICE_DESCRIPTOR *)buf;
    if (desc->Size < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return WT_ERR_WIN32;
    }

    wt_storage_copy_desc_string(d->model, sizeof(d->model) / sizeof(d->model[0]),
                                desc, desc->ProductIdOffset);
    if (d->model[0] == L'\0') {
        wt_storage_copy_desc_string(d->model,
                                    sizeof(d->model) / sizeof(d->model[0]),
                                    desc, desc->VendorIdOffset);
    }
    wt_storage_copy_desc_string(d->serial,
                                sizeof(d->serial) / sizeof(d->serial[0]),
                                desc, desc->SerialNumberOffset);
    wt_storage_bus_name(desc->BusType, d->bus_type,
                        sizeof(d->bus_type) / sizeof(d->bus_type[0]));
    d->descriptor_ok = 1;
    return WT_OK;
}

static WT_Result wt_storage_query_predict(HANDLE h, WT_StorageDiskHealth *d)
{
    STORAGE_PREDICT_FAILURE pred;
    DWORD bytes = 0;

    memset(&pred, 0, sizeof(pred));
    if (!DeviceIoControl(h, IOCTL_STORAGE_PREDICT_FAILURE, NULL, 0,
                         &pred, sizeof(pred), &bytes, NULL)) {
        DWORD err = GetLastError();
        d->predict_failure = -1;
        d->predict_ok = 0;
        if (err == ERROR_INVALID_FUNCTION || err == ERROR_NOT_SUPPORTED ||
            err == ERROR_IO_DEVICE) {
            StringCchCopyW(d->note, sizeof(d->note) / sizeof(d->note[0]),
                           L"Failure prediction not supported on this device.");
            d->status = WT_STORAGE_HEALTH_UNKNOWN;
            return WT_OK;
        }
        if (err == ERROR_ACCESS_DENIED) {
            StringCchCopyW(d->note, sizeof(d->note) / sizeof(d->note[0]),
                           L"Access denied querying failure prediction.");
            d->status = WT_STORAGE_HEALTH_UNAVAILABLE;
            return WT_ERR_ACCESS_DENIED;
        }
        d->status = WT_STORAGE_HEALTH_UNAVAILABLE;
        return WT_ERR_WIN32;
    }

    d->predict_ok = 1;
    if (pred.PredictFailure != 0) {
        d->predict_failure = 1;
        d->status = WT_STORAGE_HEALTH_DEGRADED;
        StringCchCopyW(d->note, sizeof(d->note) / sizeof(d->note[0]),
                       L"Windows reports a predicted storage failure. "
                       L"Back up data and check vendor diagnostics.");
    } else {
        d->predict_failure = 0;
        d->status = WT_STORAGE_HEALTH_OK;
    }
    return WT_OK;
}

static HANDLE wt_storage_open_drive(unsigned int index)
{
    wchar_t path[64];
    HANDLE h;

    StringCchPrintfW(path, sizeof(path) / sizeof(path[0]),
                     L"\\\\.\\PhysicalDrive%u", index);
    h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        return h;
    }
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    return h;
}

static int wt_storage_probe_disk(unsigned int index, WT_StorageDiskHealth *d)
{
    HANDLE h = wt_storage_open_drive(index);
    WT_Result r;

    if (h == INVALID_HANDLE_VALUE) {
        return 0; /* drive index does not exist */
    }

    memset(d, 0, sizeof(*d));
    d->physical_drive = index;
    d->predict_failure = -1;
    d->status = WT_STORAGE_HEALTH_UNKNOWN;
    StringCchCopyW(d->media_hint, sizeof(d->media_hint) / sizeof(d->media_hint[0]),
                   L"unknown");
    StringCchCopyW(d->bus_type, sizeof(d->bus_type) / sizeof(d->bus_type[0]),
                   L"unknown");

    (void)wt_storage_query_descriptor(h, d);
    wt_storage_query_media_hint(h, d->media_hint,
                                sizeof(d->media_hint) / sizeof(d->media_hint[0]));
    r = wt_storage_query_predict(h, d);
    if (r == WT_ERR_ACCESS_DENIED) {
        d->status = WT_STORAGE_HEALTH_UNAVAILABLE;
    }

    if (d->model[0] == L'\0') {
        StringCchPrintfW(d->model, sizeof(d->model) / sizeof(d->model[0]),
                         L"PhysicalDrive%u", index);
    }

    CloseHandle(h);
    return 1;
}

WT_Result wt_collect_storage_health(WT_StorageHealthReport *report)
{
    unsigned int i;
    size_t found = 0;

    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_storage_health_init(report);

    for (i = 0; i < WT_MAX_STORAGE_DISKS; ++i) {
        WT_StorageDiskHealth disk;
        if (!wt_storage_probe_disk(i, &disk)) {
            continue;
        }
        report->disks[found++] = disk;
        if (disk.status == WT_STORAGE_HEALTH_DEGRADED) {
            report->any_degraded = 1;
        } else if (disk.status == WT_STORAGE_HEALTH_WARNING) {
            report->any_warning = 1;
        } else if (disk.status == WT_STORAGE_HEALTH_UNAVAILABLE) {
            report->any_unavailable = 1;
        }
        if (found >= WT_MAX_STORAGE_DISKS) {
            break;
        }
    }

    report->disk_count = found;
    if (found == 0) {
        StringCchCopyW(report->note, sizeof(report->note) / sizeof(report->note[0]),
                       L"No physical disks could be opened. Try an elevated "
                       L"shell if access was denied.");
        return WT_ERR_NOT_FOUND;
    }

    if (report->any_degraded) {
        StringCchCopyW(report->note, sizeof(report->note) / sizeof(report->note[0]),
                       L"At least one disk reported a predicted failure. "
                       L"Back up immediately and use vendor tools.");
    } else if (report->any_unavailable) {
        StringCchCopyW(report->note, sizeof(report->note) / sizeof(report->note[0]),
                       L"Some disks could not be queried fully (access or "
                       L"driver limits).");
    } else {
        StringCchCopyW(report->note, sizeof(report->note) / sizeof(report->note[0]),
                       L"Read-only check only. WinTune never wipes, formats, "
                       L"or repairs disks.");
    }

    return WT_OK;
}
