#include "metrics/network.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <tcpestats.h>
#include <stdlib.h>
#include <string.h>

#define WT_NET_DEFAULT_SAMPLE_MS 500
#define WT_NET_MAX_CONNECTIONS   1024
#define WT_NET_SETTLE_MS         50
/* Reject connection deltas that cannot be real for a short sample window. */
#define WT_NET_MAX_CONN_DELTA_BYTES (256ULL * 1024ULL * 1024ULL)

typedef struct WT_TcpConnSnap {
    int family; /* AF_INET or AF_INET6 */
    DWORD pid;
    DWORD local_addr;
    DWORD remote_addr;
    DWORD local_port;
    DWORD remote_port;
    UCHAR local6[16];
    UCHAR remote6[16];
    unsigned long long bytes_in;
    unsigned long long bytes_out;
    int ok;
} WT_TcpConnSnap;

WT_Result wt_collect_net_totals(WT_NetTotals *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out->in_bytes = 0;
    out->out_bytes = 0;
    out->interface_count = 0;

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

const WT_ProcessNetRate *wt_find_process_net_rate(const WT_ProcessNetRate *rates,
                                                  size_t count,
                                                  unsigned long pid)
{
    if (rates == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (rates[i].pid == pid) {
            return &rates[i];
        }
    }
    return NULL;
}

static int wt_tcp_snap_match(const WT_TcpConnSnap *a, const WT_TcpConnSnap *b)
{
    if (a == NULL || b == NULL || a->family != b->family ||
        a->local_port != b->local_port || a->remote_port != b->remote_port) {
        return 0;
    }
    if (a->family == AF_INET) {
        return a->local_addr == b->local_addr &&
               a->remote_addr == b->remote_addr;
    }
    return memcmp(a->local6, b->local6, 16) == 0 &&
           memcmp(a->remote6, b->remote6, 16) == 0;
}

static void wt_tcp_read_estats4(MIB_TCPROW_OWNER_PID *row, WT_TcpConnSnap *snap,
                               int enable)
{
    ZeroMemory(snap, sizeof(*snap));
    snap->family = AF_INET;
    snap->pid = row->dwOwningPid;
    snap->local_addr = row->dwLocalAddr;
    snap->remote_addr = row->dwRemoteAddr;
    snap->local_port = row->dwLocalPort;
    snap->remote_port = row->dwRemotePort;

    if (enable) {
        TCP_ESTATS_DATA_RW_v0 rw;
        ZeroMemory(&rw, sizeof(rw));
        rw.EnableCollection = TRUE;
        (void)SetPerTcpConnectionEStats((PMIB_TCPROW)row, TcpConnectionEstatsData,
                                        (PUCHAR)&rw, 0, sizeof(rw), 0);
    }

    TCP_ESTATS_DATA_ROD_v0 rod;
    ZeroMemory(&rod, sizeof(rod));
    if (GetPerTcpConnectionEStats((PMIB_TCPROW)row, TcpConnectionEstatsData,
                                  NULL, 0, 0, NULL, 0, 0, (PUCHAR)&rod, 0,
                                  sizeof(rod)) == NO_ERROR) {
        snap->bytes_in = rod.DataBytesIn;
        snap->bytes_out = rod.DataBytesOut;
        snap->ok = 1;
    }
}

static void wt_tcp_read_estats6(MIB_TCP6ROW_OWNER_PID *row, WT_TcpConnSnap *snap,
                               int enable)
{
    ZeroMemory(snap, sizeof(*snap));
    snap->family = AF_INET6;
    snap->pid = row->dwOwningPid;
    snap->local_port = row->dwLocalPort;
    snap->remote_port = row->dwRemotePort;
    memcpy(snap->local6, row->ucLocalAddr, 16);
    memcpy(snap->remote6, row->ucRemoteAddr, 16);

    MIB_TCP6ROW row6;
    ZeroMemory(&row6, sizeof(row6));
    row6.State = (MIB_TCP_STATE)row->dwState;
    memcpy(&row6.LocalAddr, row->ucLocalAddr, 16);
    row6.dwLocalScopeId = row->dwLocalScopeId;
    row6.dwLocalPort = row->dwLocalPort;
    memcpy(&row6.RemoteAddr, row->ucRemoteAddr, 16);
    row6.dwRemoteScopeId = row->dwRemoteScopeId;
    row6.dwRemotePort = row->dwRemotePort;

    if (enable) {
        TCP_ESTATS_DATA_RW_v0 rw;
        ZeroMemory(&rw, sizeof(rw));
        rw.EnableCollection = TRUE;
        (void)SetPerTcp6ConnectionEStats(&row6, TcpConnectionEstatsData,
                                         (PUCHAR)&rw, 0, sizeof(rw), 0);
    }

    TCP_ESTATS_DATA_ROD_v0 rod;
    ZeroMemory(&rod, sizeof(rod));
    if (GetPerTcp6ConnectionEStats(&row6, TcpConnectionEstatsData, NULL, 0, 0,
                                   NULL, 0, 0, (PUCHAR)&rod, 0,
                                   sizeof(rod)) == NO_ERROR) {
        snap->bytes_in = rod.DataBytesIn;
        snap->bytes_out = rod.DataBytesOut;
        snap->ok = 1;
    }
}

static WT_Result wt_tcp_capture(WT_TcpConnSnap *snaps, size_t capacity,
                                size_t *out_count, int enable)
{
    if (snaps == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    ULONG size = 0;
    DWORD rc = GetExtendedTcpTable(NULL, &size, FALSE, AF_INET,
                                   TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (rc == ERROR_INSUFFICIENT_BUFFER && size > 0) {
        MIB_TCPTABLE_OWNER_PID *table = (MIB_TCPTABLE_OWNER_PID *)malloc(size);
        if (table == NULL) {
            return WT_ERR_OUT_OF_MEMORY;
        }
        rc = GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                                 TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (rc == NO_ERROR) {
            for (DWORD i = 0; i < table->dwNumEntries && *out_count < capacity;
                 ++i) {
                wt_tcp_read_estats4(&table->table[i], &snaps[*out_count],
                                    enable);
                if (snaps[*out_count].ok) {
                    (*out_count)++;
                }
            }
        }
        free(table);
    }

    size = 0;
    rc = GetExtendedTcpTable(NULL, &size, FALSE, AF_INET6,
                             TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (rc == ERROR_INSUFFICIENT_BUFFER && size > 0) {
        MIB_TCP6TABLE_OWNER_PID *table6 =
            (MIB_TCP6TABLE_OWNER_PID *)malloc(size);
        if (table6 == NULL) {
            return (*out_count > 0) ? WT_OK : WT_ERR_OUT_OF_MEMORY;
        }
        rc = GetExtendedTcpTable(table6, &size, FALSE, AF_INET6,
                                 TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (rc == NO_ERROR) {
            for (DWORD i = 0; i < table6->dwNumEntries && *out_count < capacity;
                 ++i) {
                wt_tcp_read_estats6(&table6->table[i], &snaps[*out_count],
                                    enable);
                if (snaps[*out_count].ok) {
                    (*out_count)++;
                }
            }
        }
        free(table6);
    }

    return WT_OK;
}

static void wt_net_rate_add(WT_ProcessNetRate *out, size_t capacity,
                            size_t *count, DWORD pid, double recv_bps,
                            double send_bps)
{
    if (pid == 0) {
        return;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (out[i].pid == pid) {
            out[i].recv_bytes_per_sec += recv_bps;
            out[i].send_bytes_per_sec += send_bps;
            return;
        }
    }
    if (*count >= capacity) {
        return;
    }
    out[*count].pid = pid;
    out[*count].recv_bytes_per_sec = recv_bps;
    out[*count].send_bytes_per_sec = send_bps;
    (*count)++;
}

void wt_net_tcp_sample_abort(WT_NetTcpSampleSession *session)
{
    if (session == NULL) {
        return;
    }
    if (session->before != NULL) {
        free(session->before);
        session->before = NULL;
    }
    session->before_count = 0;
    session->active = 0;
}

WT_Result wt_net_tcp_sample_start(WT_NetTcpSampleSession *session,
                                  unsigned int sample_ms)
{
    if (session == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(session, sizeof(*session));
    session->sample_ms = sample_ms ? sample_ms : WT_NET_DEFAULT_SAMPLE_MS;

    WT_TcpConnSnap *scratch =
        (WT_TcpConnSnap *)calloc(WT_NET_MAX_CONNECTIONS, sizeof(WT_TcpConnSnap));
    if (scratch == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    /* Enable collection first; the first read after enable can include
     * pre-enable cumulative bytes on some systems, so discard it. */
    size_t discard_count = 0;
    WT_Result r =
        wt_tcp_capture(scratch, WT_NET_MAX_CONNECTIONS, &discard_count, 1);
    if (r != WT_OK) {
        free(scratch);
        return r;
    }
    Sleep(WT_NET_SETTLE_MS);

    size_t before_count = 0;
    r = wt_tcp_capture(scratch, WT_NET_MAX_CONNECTIONS, &before_count, 0);
    if (r != WT_OK) {
        free(scratch);
        return r;
    }

    session->before = scratch;
    session->before_count = before_count;
    session->active = 1;
    return WT_OK;
}

WT_Result wt_net_tcp_sample_finish(WT_NetTcpSampleSession *session,
                                   WT_ProcessNetRate *out,
                                   size_t capacity,
                                   size_t *out_count)
{
    if (session == NULL || out == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    if (!session->active || session->before == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    WT_TcpConnSnap *before = (WT_TcpConnSnap *)session->before;
    size_t before_count = session->before_count;

    WT_TcpConnSnap *after =
        (WT_TcpConnSnap *)calloc(WT_NET_MAX_CONNECTIONS, sizeof(WT_TcpConnSnap));
    if (after == NULL) {
        wt_net_tcp_sample_abort(session);
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t after_count = 0;
    WT_Result r =
        wt_tcp_capture(after, WT_NET_MAX_CONNECTIONS, &after_count, 0);
    if (r != WT_OK) {
        free(after);
        wt_net_tcp_sample_abort(session);
        return r;
    }

    const double secs = (double)session->sample_ms / 1000.0;
    if (secs > 0.0) {
        for (size_t i = 0; i < after_count; ++i) {
            const WT_TcpConnSnap *a = &after[i];
            if (!a->ok) {
                continue;
            }
            const WT_TcpConnSnap *b = NULL;
            for (size_t j = 0; j < before_count; ++j) {
                if (wt_tcp_snap_match(a, &before[j])) {
                    b = &before[j];
                    break;
                }
            }
            if (b == NULL || !b->ok) {
                continue;
            }
            unsigned long long din = 0;
            unsigned long long dout = 0;
            if (a->bytes_in >= b->bytes_in) {
                din = a->bytes_in - b->bytes_in;
            }
            if (a->bytes_out >= b->bytes_out) {
                dout = a->bytes_out - b->bytes_out;
            }
            if (din > WT_NET_MAX_CONN_DELTA_BYTES ||
                dout > WT_NET_MAX_CONN_DELTA_BYTES) {
                continue;
            }
            if (din == 0 && dout == 0) {
                continue;
            }
            wt_net_rate_add(out, capacity, out_count, a->pid,
                            (double)din / secs, (double)dout / secs);
        }
    }

    free(after);
    wt_net_tcp_sample_abort(session);
    return WT_OK;
}

WT_Result wt_collect_process_net_rates(unsigned int sample_ms,
                                       WT_ProcessNetRate *out,
                                       size_t capacity,
                                       size_t *out_count)
{
    WT_NetTcpSampleSession session;
    WT_Result r = wt_net_tcp_sample_start(&session, sample_ms);
    if (r != WT_OK) {
        return r;
    }
    Sleep(session.sample_ms);
    return wt_net_tcp_sample_finish(&session, out, capacity, out_count);
}
