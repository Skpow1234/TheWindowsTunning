#include "system/power.h"

#include <windows.h>
#include <powrprof.h>
#include <strsafe.h>
#include <stdlib.h>

/* Standard Windows power scheme GUIDs. These are fixed across locales, so
 * matching by GUID avoids depending on the (localized) friendly name. */
static const GUID WT_GUID_BALANCED =
    {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID WT_GUID_HIGH_PERF =
    {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID WT_GUID_POWER_SAVER =
    {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
static const GUID WT_GUID_ULTIMATE =
    {0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};

const char *wt_power_scheme_name(WT_PowerScheme scheme)
{
    switch (scheme) {
    case WT_POWER_BALANCED:     return "Balanced";
    case WT_POWER_HIGH_PERF:    return "High performance";
    case WT_POWER_POWER_SAVER:  return "Power saver";
    case WT_POWER_ULTIMATE:     return "Ultimate performance";
    default:                    return "Unknown";
    }
}

static void wt_power_read_friendly_name(const GUID *scheme, wchar_t *out, size_t count)
{
    DWORD bytes = 0;
    if (PowerReadFriendlyName(NULL, scheme, NULL, NULL, NULL, &bytes) != ERROR_SUCCESS
            || bytes == 0) {
        return;
    }

    BYTE *buffer = (BYTE *)malloc(bytes);
    if (buffer == NULL) {
        return;
    }
    if (PowerReadFriendlyName(NULL, scheme, NULL, NULL, buffer, &bytes) == ERROR_SUCCESS) {
        StringCchCopyW(out, count, (const wchar_t *)buffer);
    }
    free(buffer);
}

WT_Result wt_collect_power_info(WT_PowerInfo *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    out->scheme = WT_POWER_UNKNOWN;
    out->on_ac = -1;
    out->battery_percent = -1;
    StringCchCopyW(out->active_name, ARRAYSIZE(out->active_name), L"Unknown");

    GUID *active = NULL;
    if (PowerGetActiveScheme(NULL, &active) == ERROR_SUCCESS && active != NULL) {
        if (IsEqualGUID(*active, WT_GUID_BALANCED)) {
            out->scheme = WT_POWER_BALANCED;
        } else if (IsEqualGUID(*active, WT_GUID_HIGH_PERF)) {
            out->scheme = WT_POWER_HIGH_PERF;
        } else if (IsEqualGUID(*active, WT_GUID_POWER_SAVER)) {
            out->scheme = WT_POWER_POWER_SAVER;
        } else if (IsEqualGUID(*active, WT_GUID_ULTIMATE)) {
            out->scheme = WT_POWER_ULTIMATE;
        }

        /* Prefer the real (possibly localized) friendly name for display. */
        wt_power_read_friendly_name(active, out->active_name,
                                    ARRAYSIZE(out->active_name));
        LocalFree(active);
    }

    SYSTEM_POWER_STATUS status;
    if (GetSystemPowerStatus(&status)) {
        if (status.ACLineStatus == 0) {
            out->on_ac = 0;
        } else if (status.ACLineStatus == 1) {
            out->on_ac = 1;
        }
        if (status.BatteryLifePercent <= 100) {
            out->battery_percent = status.BatteryLifePercent;
        }
    }

    return WT_OK;
}
