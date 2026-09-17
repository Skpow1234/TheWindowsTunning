#ifndef WINTUNE_NETWORK_H
#define WINTUNE_NETWORK_H

#include <stddef.h>

#include "common/error.h"

typedef struct WT_NetTotals {
    unsigned long long in_bytes;   /* cumulative received octets */
    unsigned long long out_bytes;  /* cumulative sent octets */
    unsigned int interface_count;  /* operational, non-loopback interfaces */
} WT_NetTotals;

/* Per-process TCP send/recv rates from IP Helper Extended Stats (Phase 27).
 * Opt-in only: enabling EStats on many connections has measurable overhead.
 * Does not capture payloads. UDP is not included. */
typedef struct WT_ProcessNetRate {
    unsigned long pid;
    double recv_bytes_per_sec;
    double send_bytes_per_sec;
} WT_ProcessNetRate;

/* Opaque begin/end sample so callers can Sleep once shared with CPU/disk. */
typedef struct WT_NetTcpSampleSession {
    void *before; /* internal WT_TcpConnSnap* */
    size_t before_count;
    unsigned int sample_ms;
    int active;
} WT_NetTcpSampleSession;

/* Sums cumulative received/sent bytes across operational, non-loopback network
 * interfaces (GetIfTable). Read-only. Callers compute throughput by sampling
 * twice and dividing the delta by the elapsed time. */
WT_Result wt_collect_net_totals(WT_NetTotals *out);

/* Begin TCP EStats capture (enables collection). Pair with finish after Sleep. */
WT_Result wt_net_tcp_sample_start(WT_NetTcpSampleSession *session,
                                  unsigned int sample_ms);

/* End capture, aggregate rates by PID, free session buffers. */
WT_Result wt_net_tcp_sample_finish(WT_NetTcpSampleSession *session,
                                   WT_ProcessNetRate *out,
                                   size_t capacity,
                                   size_t *out_count);

/* Abort an open session without producing rates. */
void wt_net_tcp_sample_abort(WT_NetTcpSampleSession *session);

/* Convenience: start + Sleep(sample_ms) + finish. */
WT_Result wt_collect_process_net_rates(unsigned int sample_ms,
                                       WT_ProcessNetRate *out,
                                       size_t capacity,
                                       size_t *out_count);

const WT_ProcessNetRate *wt_find_process_net_rate(const WT_ProcessNetRate *rates,
                                                  size_t count,
                                                  unsigned long pid);

#endif /* WINTUNE_NETWORK_H */
