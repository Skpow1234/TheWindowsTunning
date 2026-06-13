#ifndef WINTUNE_SERVICE_IPC_H
#define WINTUNE_SERVICE_IPC_H

#include <stddef.h>
#include <windows.h>

#include "common/error.h"

#define WT_IPC_PIPE_NAME L"\\\\.\\pipe\\WinTune"
#define WT_IPC_MAGIC     0x31504957u /* 'WIP1' little-endian on wire */

/* Sends a UTF-8 JSON request and receives a UTF-8 JSON payload.
 * Caller must free(*response_out) with free(). */
WT_Result wt_service_ipc_call(const char *request_json,
                              char **response_out,
                              size_t *response_len,
                              unsigned timeout_ms);

/* Server-side helpers (service process only). */
WT_Result wt_service_ipc_send_response(HANDLE pipe, const char *response_json,
                                       size_t response_len);
WT_Result wt_service_ipc_read_request(HANDLE pipe, char **request_out,
                                      size_t *request_len);
WT_Result wt_service_ipc_create_pipe_instance(HANDLE *out_pipe);

#endif /* WINTUNE_SERVICE_IPC_H */
