#ifndef WINTUNE_NETWORK_H
#define WINTUNE_NETWORK_H

#include "common/error.h"

typedef struct WT_NetTotals {
    unsigned long long in_bytes;   /* cumulative received octets */
    unsigned long long out_bytes;  /* cumulative sent octets */
    unsigned int interface_count;  /* operational, non-loopback interfaces */
} WT_NetTotals;

/* Sums cumulative received/sent bytes across operational, non-loopback network
 * interfaces (GetIfTable2). Read-only. Callers compute throughput by sampling
 * twice and dividing the delta by the elapsed time. */
WT_Result wt_collect_net_totals(WT_NetTotals *out);

#endif /* WINTUNE_NETWORK_H */
