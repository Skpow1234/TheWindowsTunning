#include "metrics/network.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <stdlib.h>

WT_Result wt_collect_net_totals(WT_NetTotals *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out->in_bytes = 0;
    out->out_bytes = 0;
    out->interface_count = 0;

    /* Size the table, then fetch it (it can grow between calls, so loop). */
    ULONG size = 0;
    DWORD rc = GetIfTable(NULL, &size, FALSE);
    if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return WT_ERR_WIN32;
    }

    MIB_IFTABLE *table = (MIB_IFTABLE *)malloc(size);
    if (table == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    rc = GetIfTable(table, &size, FALSE);
    if (rc != NO_ERROR) {
        free(table);
        return WT_ERR_WIN32;
    }

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW *row = &table->table[i];
        if (row->dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        out->in_bytes += row->dwInOctets;
        out->out_bytes += row->dwOutOctets;
        out->interface_count++;
    }

    free(table);
    return WT_OK;
}
