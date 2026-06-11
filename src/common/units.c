#include "common/units.h"

#include <windows.h>
#include <strsafe.h>

WT_Result wt_format_bytes(unsigned long long bytes, wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    static const wchar_t *units[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB" };
    const size_t max_unit = (sizeof(units) / sizeof(units[0])) - 1;

    double value = (double)bytes;
    size_t unit = 0;
    while (value >= 1024.0 && unit < max_unit) {
        value /= 1024.0;
        unit++;
    }

    HRESULT hr;
    if (unit == 0) {
        hr = StringCchPrintfW(out, count, L"%llu %s", bytes, units[unit]);
    } else {
        hr = StringCchPrintfW(out, count, L"%.1f %s", value, units[unit]);
    }
    return FAILED(hr) ? WT_ERR_BUFFER_TOO_SMALL : WT_OK;
}

WT_Result wt_format_duration_ms(unsigned long long ms, wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    unsigned long long total_seconds = ms / 1000ULL;
    unsigned long long days = total_seconds / 86400ULL;
    unsigned long long hours = (total_seconds % 86400ULL) / 3600ULL;
    unsigned long long minutes = (total_seconds % 3600ULL) / 60ULL;

    HRESULT hr;
    if (days > 0) {
        hr = StringCchPrintfW(out, count, L"%llud %02lluh", days, hours);
    } else if (hours > 0) {
        hr = StringCchPrintfW(out, count, L"%lluh %02llum", hours, minutes);
    } else {
        hr = StringCchPrintfW(out, count, L"%llum", minutes);
    }
    return FAILED(hr) ? WT_ERR_BUFFER_TOO_SMALL : WT_OK;
}
