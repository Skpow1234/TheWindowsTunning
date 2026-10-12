#include "metrics/network.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <tcpestats.h>
#include <evntrace.h>
#include <evntcons.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

#define WT_NET_DEFAULT_SAMPLE_MS 500
#define WT_NET_MAX_CONNECTIONS   1024
#define WT_NET_SETTLE_MS         50
/* Reject connection deltas that cannot be real for a short sample window. */
#define WT_NET_MAX_CONN_DELTA_BYTES (256ULL * 1024ULL * 1024ULL)
#define WT_UDP_MAX_PIDS 256
#define WT_UDP_EVENT_SEND 1169
#define WT_UDP_EVENT_RECV 1170
/* Microsoft-Windows-TCPIP keywords ut:SendPath and ut:ReceivePath. */
#define WT_UDP_KW_SEND 0x100000000ULL
#define WT_UDP_KW_RECV 0x200000000ULL

static const GUID WT_TCPIP_PROVIDER = {
    0x2F07E2EE, 0x15DB, 0x40F1,
    {0x90, 0xEF, 0x9D, 0x7B, 0xA2, 0x82, 0x18, 0x8A}
};

typedef struct WT_UdpAccum {
    unsigned long pid;
    unsigned long long send_bytes;
    unsigned long long recv_bytes;
} WT_UdpAccum;

typedef struct WT_UdpTrace {
    TRACEHANDLE session;
    TRACEHANDLE open;
    HANDLE thread;
    wchar_t name[64];
    EVENT_TRACE_PROPERTIES *prop;
    WT_UdpAccum rows[WT_UDP_MAX_PIDS];
    size_t count;
    int started;
} WT_UdpTrace;

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
    out[*count].udp_recv_bytes_per_sec = -1.0;
    out[*count].udp_send_bytes_per_sec = -1.0;
    (*count)++;
}

static void wt_udp_add(WT_UdpTrace *trace, unsigned long pid,
                       unsigned long long bytes, int is_send)
{
    if (trace == NULL || pid == 0 || bytes == 0) {
        return;
    }
    for (size_t i = 0; i < trace->count; ++i) {
        if (trace->rows[i].pid == pid) {
            if (is_send) {
                trace->rows[i].send_bytes += bytes;
            } else {
                trace->rows[i].recv_bytes += bytes;
            }
            return;
        }
    }
    if (trace->count >= WT_UDP_MAX_PIDS) {
        return;
    }
    trace->rows[trace->count].pid = pid;
    trace->rows[trace->count].send_bytes = is_send ? bytes : 0;
    trace->rows[trace->count].recv_bytes = is_send ? 0 : bytes;
    trace->count++;
}

/* Summary events 1169/1170: pointer, message count, byte count, then two
 * socket-address blobs, then the PID. Addresses are skipped and not stored. */
static int wt_udp_parse_summary(const EVENT_RECORD *rec, unsigned long *pid,
                                unsigned long *bytes)
{
    if (rec == NULL || rec->UserData == NULL || pid == NULL || bytes == NULL) {
        return 0;
    }
    const BYTE *p = (const BYTE *)rec->UserData;
    ULONG left = rec->UserDataLength;
    size_t ptr = sizeof(ULONG_PTR);
    if (left < ptr + 8) {
        return 0;
    }
    p += ptr;
    left -= (ULONG)ptr;
    if (left < 8) {
        return 0;
    }
    p += 4;
    left -= 4;
    unsigned long nbytes = 0;
    memcpy(&nbytes, p, 4);
    p += 4;
    left -= 4;
    if (left < 4) {
        return 0;
    }
    unsigned long local_len = 0;
    memcpy(&local_len, p, 4);
    p += 4;
    left -= 4;
    if (local_len > left) {
        return 0;
    }
    p += local_len;
    left -= local_len;
    if (left < 4) {
        return 0;
    }
    unsigned long remote_len = 0;
    memcpy(&remote_len, p, 4);
    p += 4;
    left -= 4;
    if (remote_len > left) {
        return 0;
    }
    p += remote_len;
    left -= remote_len;
    if (left < 4) {
        return 0;
    }
    unsigned long event_pid = 0;
    memcpy(&event_pid, p, 4);
    *pid = event_pid;
    *bytes = nbytes;
    return 1;
}

static VOID WINAPI wt_udp_on_event(PEVENT_RECORD rec)
{
    if (rec == NULL || rec->UserContext == NULL) {
        return;
    }
    USHORT id = rec->EventHeader.EventDescriptor.Id;
    if (id != WT_UDP_EVENT_SEND && id != WT_UDP_EVENT_RECV) {
        return;
    }
    unsigned long pid = 0;
    unsigned long bytes = 0;
    if (!wt_udp_parse_summary(rec, &pid, &bytes)) {
        return;
    }
    wt_udp_add((WT_UdpTrace *)rec->UserContext, pid, bytes,
               id == WT_UDP_EVENT_SEND);
}

static DWORD WINAPI wt_udp_thread(LPVOID param)
{
    WT_UdpTrace *trace = (WT_UdpTrace *)param;
    if (trace == NULL) {
        return 0;
    }
    TRACEHANDLE open = trace->open;
    (void)ProcessTrace(&open, 1, NULL, NULL);
    return 0;
}

static void wt_udp_stop(WT_UdpTrace *trace)
{
    if (trace == NULL) {
        return;
    }
    if (trace->started && trace->prop != NULL) {
        (void)ControlTraceW(trace->session, trace->name, trace->prop,
                            EVENT_TRACE_CONTROL_STOP);
        trace->started = 0;
    }
    if (trace->open != 0 && trace->open != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(trace->open);
        trace->open = 0;
    }
    if (trace->thread != NULL) {
        WaitForSingleObject(trace->thread, 3000);
        CloseHandle(trace->thread);
        trace->thread = NULL;
    }
    if (trace->prop != NULL) {
        free(trace->prop);
        trace->prop = NULL;
    }
}

static int wt_udp_start(WT_UdpTrace *trace)
{
    if (trace == NULL) {
        return 0;
    }
    ZeroMemory(trace, sizeof(*trace));
    if (FAILED(StringCchPrintfW(trace->name, ARRAYSIZE(trace->name),
                                L"WinTune-UDP-%lu", GetCurrentProcessId()))) {
        return 0;
    }

    size_t name_chars = wcslen(trace->name) + 1;
    ULONG buffer_size =
        (ULONG)(sizeof(EVENT_TRACE_PROPERTIES) + name_chars * sizeof(wchar_t));
    trace->prop = (EVENT_TRACE_PROPERTIES *)calloc(1, buffer_size);
    if (trace->prop == NULL) {
        return 0;
    }
    trace->prop->Wnode.BufferSize = buffer_size;
    trace->prop->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    trace->prop->Wnode.ClientContext = 1;
    trace->prop->LogFileMode =
        EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    trace->prop->BufferSize = 64;
    trace->prop->MinimumBuffers = 4;
    trace->prop->MaximumBuffers = 16;
    trace->prop->FlushTimer = 1;
    trace->prop->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    wchar_t *name_slot =
        (wchar_t *)((BYTE *)trace->prop + trace->prop->LoggerNameOffset);
    wcscpy_s(name_slot, name_chars, trace->name);

    ULONG rc = StartTraceW(&trace->session, trace->name, trace->prop);
    if (rc == ERROR_ALREADY_EXISTS) {
        (void)ControlTraceW(0, trace->name, trace->prop, EVENT_TRACE_CONTROL_STOP);
        rc = StartTraceW(&trace->session, trace->name, trace->prop);
    }
    if (rc != ERROR_SUCCESS) {
        free(trace->prop);
        trace->prop = NULL;
        return 0;
    }
    trace->started = 1;

    ULONG enable = EnableTraceEx2(
        trace->session, &WT_TCPIP_PROVIDER, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
        TRACE_LEVEL_INFORMATION, WT_UDP_KW_SEND | WT_UDP_KW_RECV, 0, 0, NULL);
    if (enable != ERROR_SUCCESS) {
        wt_udp_stop(trace);
        return 0;
    }

    EVENT_TRACE_LOGFILEW log;
    ZeroMemory(&log, sizeof(log));
    log.LoggerName = trace->name;
    log.ProcessTraceMode =
        PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = wt_udp_on_event;
    log.Context = trace;
    trace->open = OpenTraceW(&log);
    if (trace->open == INVALID_PROCESSTRACE_HANDLE) {
        wt_udp_stop(trace);
        return 0;
    }

    trace->thread = CreateThread(NULL, 0, wt_udp_thread, trace, 0, NULL);
    if (trace->thread == NULL) {
        wt_udp_stop(trace);
        return 0;
    }
    return 1;
}

void wt_net_tcp_sample_abort(WT_NetTcpSampleSession *session)
{
    if (session == NULL) {
        return;
    }
    if (session->udp != NULL) {
        wt_udp_stop((WT_UdpTrace *)session->udp);
        free(session->udp);
        session->udp = NULL;
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

    WT_UdpTrace *udp = (WT_UdpTrace *)calloc(1, sizeof(WT_UdpTrace));
    if (udp != NULL && wt_udp_start(udp)) {
        session->udp = udp;
        session->udp_measured = 1;
    } else {
        free(udp);
        session->udp = NULL;
        session->udp_measured = 0;
    }
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

    WT_UdpTrace *udp = (WT_UdpTrace *)session->udp;
    if (udp != NULL) {
        wt_udp_stop(udp);
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

    const int udp_measured = session->udp_measured;
    for (size_t i = 0; i < *out_count; ++i) {
        out[i].udp_recv_bytes_per_sec = udp_measured ? 0.0 : -1.0;
        out[i].udp_send_bytes_per_sec = udp_measured ? 0.0 : -1.0;
    }
    if (udp_measured && udp != NULL && secs > 0.0) {
        for (size_t i = 0; i < udp->count; ++i) {
            double recv = (double)udp->rows[i].recv_bytes / secs;
            double send = (double)udp->rows[i].send_bytes / secs;
            const WT_ProcessNetRate *existing =
                wt_find_process_net_rate(out, *out_count, udp->rows[i].pid);
            if (existing != NULL) {
                WT_ProcessNetRate *slot = (WT_ProcessNetRate *)existing;
                slot->udp_recv_bytes_per_sec = recv;
                slot->udp_send_bytes_per_sec = send;
            } else {
                wt_net_rate_add(out, capacity, out_count, udp->rows[i].pid, 0.0,
                                0.0);
                if (*out_count > 0 && out[*out_count - 1].pid == udp->rows[i].pid) {
                    out[*out_count - 1].udp_recv_bytes_per_sec = recv;
                    out[*out_count - 1].udp_send_bytes_per_sec = send;
                }
            }
        }
    }

    free(after);
    wt_net_tcp_sample_abort(session);
    session->udp_measured = udp_measured;
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
