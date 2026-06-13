#ifndef WINTUNE_SERVICE_CLIENT_H
#define WINTUNE_SERVICE_CLIENT_H

#include <stddef.h>
#include <stdio.h>

#include "common/error.h"

/* Returns 1 if the WinTune pipe responds to ping. */
int wt_service_client_is_available(unsigned timeout_ms);

/* Sends scan/doctor IPC and writes JSON payload to `out`. */
WT_Result wt_service_client_scan(int doctor_mode, long interval_ms, FILE *out);

/* Sends apply IPC. Prints message to stdout on success. */
WT_Result wt_service_client_apply(const wchar_t *id, int assume_yes,
                                  char *msg, size_t msg_cap);

#endif /* WINTUNE_SERVICE_CLIENT_H */
