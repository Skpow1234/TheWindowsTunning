#include "system/os_info.h"

#include <windows.h>
#include <strsafe.h>
#include <stdlib.h>

#define WT_REG_CURRENT_VERSION L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

static WT_Result wt_reg_read_string(const wchar_t *value_name,
                                    wchar_t *out,
                                    size_t out_count)
{
    DWORD bytes = (DWORD)(out_count * sizeof(wchar_t));
    LSTATUS st = RegGetValueW(HKEY_LOCAL_MACHINE, WT_REG_CURRENT_VERSION,
                              value_name, RRF_RT_REG_SZ, NULL, out, &bytes);
    return (st == ERROR_SUCCESS) ? WT_OK : WT_ERR_NOT_FOUND;
}

static unsigned long wt_reg_read_build_number(void)
{
    wchar_t build[32] = {0};
    if (wt_reg_read_string(L"CurrentBuildNumber", build, ARRAYSIZE(build)) != WT_OK) {
        return 0;
    }
    return (unsigned long)wcstoul(build, NULL, 10);
}

static void wt_fill_arch(wchar_t *out, size_t count)
{
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);

    const wchar_t *arch;
    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = L"x64";     break;
    case PROCESSOR_ARCHITECTURE_ARM64: arch = L"arm64";   break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = L"x86";     break;
    default:                           arch = L"unknown"; break;
    }
    StringCchCopyW(out, count, arch);
}

WT_Result wt_collect_os_info(WT_OsInfo *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));

    if (wt_reg_read_string(L"ProductName", out->product_name,
                           ARRAYSIZE(out->product_name)) != WT_OK) {
        StringCchCopyW(out->product_name, ARRAYSIZE(out->product_name), L"Windows");
    }

    /* The registry ProductName still reports "Windows 10" on Windows 11; the
     * build number (>= 22000) is the reliable signal for Windows 11. */
    if (wt_reg_read_build_number() >= 22000) {
        wchar_t *win10 = wcsstr(out->product_name, L"Windows 10");
        if (win10 != NULL) {
            /* "Windows 10" and "Windows 11" are the same length: patch in place. */
            win10[9] = L'1';
        }
    }

    wt_fill_arch(out->arch, ARRAYSIZE(out->arch));

    DWORD host_count = ARRAYSIZE(out->hostname);
    if (!GetComputerNameExW(ComputerNameDnsHostname, out->hostname, &host_count)) {
        StringCchCopyW(out->hostname, ARRAYSIZE(out->hostname), L"(unknown)");
    }

    out->uptime_ms = GetTickCount64();
    return WT_OK;
}
