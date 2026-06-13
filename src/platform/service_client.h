#ifndef WINTUNE_SERVICE_CLIENT_H
#define WINTUNE_SERVICE_CLIENT_H

#include <stddef.h>
#include <stdio.h>

#include "common/error.h"

/* Returns 1 if the WinTune pipe responds to ping. */
int wt_service_client_is_available(unsigned timeout_ms);

/* Sends scan/doctor IPC. When text_format is set, requests human-readable text. */
WT_Result wt_service_client_scan(int doctor_mode, long interval_ms,
                                 int text_format, FILE *out);

WT_Result wt_service_client_apply(const wchar_t *id, int assume_yes,
                                  char *msg, size_t msg_cap);

WT_Result wt_service_client_power_set(const wchar_t *plan_token, int assume_yes,
                                      char *msg, size_t msg_cap);

WT_Result wt_service_client_restart_service(const wchar_t *name, int assume_yes,
                                            char *msg, size_t msg_cap);

WT_Result wt_service_client_startup_set(const wchar_t *id, int enable,
                                        int assume_yes,
                                        char *msg, size_t msg_cap);

#endif /* WINTUNE_SERVICE_CLIENT_H */
