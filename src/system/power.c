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

static const GUID *wt_power_scheme_guid(WT_PowerScheme scheme)
{
    switch (scheme) {
    case WT_POWER_BALANCED:    return &WT_GUID_BALANCED;
    case WT_POWER_HIGH_PERF:   return &WT_GUID_HIGH_PERF;
    case WT_POWER_POWER_SAVER: return &WT_GUID_POWER_SAVER;
    case WT_POWER_ULTIMATE:    return &WT_GUID_ULTIMATE;
    default:                   return NULL;
    }
}

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
        if (IsEqualGUID(active, &WT_GUID_BALANCED)) {
            out->scheme = WT_POWER_BALANCED;
        } else if (IsEqualGUID(active, &WT_GUID_HIGH_PERF)) {
            out->scheme = WT_POWER_HIGH_PERF;
        } else if (IsEqualGUID(active, &WT_GUID_POWER_SAVER)) {
            out->scheme = WT_POWER_POWER_SAVER;
        } else if (IsEqualGUID(active, &WT_GUID_ULTIMATE)) {
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

WT_PowerScheme wt_power_scheme_from_token(const wchar_t *token)
{
    if (token == NULL) {
        return WT_POWER_UNKNOWN;
    }
    if (_wcsicmp(token, L"balanced") == 0) {
        return WT_POWER_BALANCED;
    }
    if (_wcsicmp(token, L"performance") == 0 ||
        _wcsicmp(token, L"high") == 0 ||
        _wcsicmp(token, L"high-performance") == 0) {
        return WT_POWER_HIGH_PERF;
    }
    if (_wcsicmp(token, L"saver") == 0 ||
        _wcsicmp(token, L"powersaver") == 0 ||
        _wcsicmp(token, L"power-saver") == 0) {
        return WT_POWER_POWER_SAVER;
    }
    if (_wcsicmp(token, L"ultimate") == 0) {
        return WT_POWER_ULTIMATE;
    }
    return WT_POWER_UNKNOWN;
}

static WT_Result wt_guid_to_string(const GUID *g, wchar_t *out, size_t count)
{
    HRESULT hr = StringCchPrintfW(
        out, count,
        L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        (unsigned long)g->Data1,
        (unsigned int)g->Data2,
        (unsigned int)g->Data3,
        (unsigned int)g->Data4[0], (unsigned int)g->Data4[1],
        (unsigned int)g->Data4[2], (unsigned int)g->Data4[3],
        (unsigned int)g->Data4[4], (unsigned int)g->Data4[5],
        (unsigned int)g->Data4[6], (unsigned int)g->Data4[7]);
    return SUCCEEDED(hr) ? WT_OK : WT_ERR_BUFFER_TOO_SMALL;
}

static WT_Result wt_guid_from_string(const wchar_t *s, GUID *out)
{
    unsigned long d1 = 0;
    unsigned int d2 = 0, d3 = 0;
    unsigned int b[8] = {0};
    int n = swscanf_s(
        s,
        L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
        &d1, &d2, &d3,
        &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
    if (n != 11) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out->Data1 = (DWORD)d1;
    out->Data2 = (WORD)d2;
    out->Data3 = (WORD)d3;
    for (int i = 0; i < 8; ++i) {
        out->Data4[i] = (BYTE)b[i];
    }
    return WT_OK;
}

WT_Result wt_power_get_active_guid_string(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    GUID *active = NULL;
    if (PowerGetActiveScheme(NULL, &active) != ERROR_SUCCESS || active == NULL) {
        return WT_ERR_WIN32;
    }
    WT_Result r = wt_guid_to_string(active, out, count);
    LocalFree(active);
    return r;
}

WT_Result wt_power_set_active_scheme(WT_PowerScheme scheme)
{
    const GUID *g = wt_power_scheme_guid(scheme);
    if (g == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    /* Confirm the scheme actually exists before switching: a friendly-name
     * read for a missing scheme fails, which lets us return a clear error
     * instead of silently doing nothing. */
    DWORD bytes = 0;
    if (PowerReadFriendlyName(NULL, g, NULL, NULL, NULL, &bytes) != ERROR_SUCCESS) {
        return WT_ERR_NOT_FOUND;
    }

    GUID local = *g;
    DWORD rc = PowerSetActiveScheme(NULL, &local);
    if (rc == ERROR_FILE_NOT_FOUND) {
        return WT_ERR_NOT_FOUND;
    }
    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    return (rc == ERROR_SUCCESS) ? WT_OK : WT_ERR_WIN32;
}

WT_Result wt_power_set_active_guid_string(const wchar_t *guid_str)
{
    if (guid_str == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    GUID g;
    WT_Result r = wt_guid_from_string(guid_str, &g);
    if (r != WT_OK) {
        return r;
    }
    DWORD rc = PowerSetActiveScheme(NULL, &g);
    if (rc == ERROR_FILE_NOT_FOUND) {
        return WT_ERR_NOT_FOUND;
    }
    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    return (rc == ERROR_SUCCESS) ? WT_OK : WT_ERR_WIN32;
}
