#ifndef WINTUNE_OS_INFO_H
#define WINTUNE_OS_INFO_H

#include <wchar.h>

#include "common/error.h"

typedef struct WT_OsInfo {
    wchar_t product_name[128];   /* e.g. "Windows 11 Pro" */
    wchar_t arch[16];            /* "x64", "arm64", "x86" */
    wchar_t hostname[64];
    unsigned long long uptime_ms;
} WT_OsInfo;

/* Collects basic OS identity: product name (read-only from the registry,
 * adjusted to Windows 11 when the build number indicates it), native
 * architecture, hostname, and uptime. */
WT_Result wt_collect_os_info(WT_OsInfo *out);

#endif /* WINTUNE_OS_INFO_H */
