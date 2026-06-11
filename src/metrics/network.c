#include "metrics/network.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

WT_Result wt_collect_net_totals(WT_NetTotals *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out->in_bytes = 0;
    out->out_bytes = 0;
    out->interface_count = 0;

    PMIB_IF_TABLE2 table = NULL;
    if (GetIfTable2(&table) != NO_ERROR || table == NULL) {
        return WT_ERR_WIN32;
    }

    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2 *row = &table->Table[i];
        if (row->Type == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        if (row->OperStatus != IfOperStatusUp) {
            continue;
        }
        out->in_bytes += row->InOctets;
        out->out_bytes += row->OutOctets;
        out->interface_count++;
    }

    FreeMibTable(table);
    return WT_OK;
}
