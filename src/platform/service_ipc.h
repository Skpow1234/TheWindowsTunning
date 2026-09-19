#ifndef WINTUNE_SERVICE_IPC_H
#define WINTUNE_SERVICE_IPC_H

#include <stddef.h>
#include <stdio.h>
#include <windows.h>

#include "common/error.h"

#define WT_IPC_PIPE_NAME L"\\\\.\\pipe\\WinTune"
#define WT_IPC_MAGIC     0x31504957u /* 'WIP1' little-endian on wire */

/* Named-pipe ACL modes (Phase 45). Stored under ProgramData; explicit + removable. */
typedef enum WT_IpcAclMode {
    WT_IPC_ACL_ADMIN = 0,      /* SYSTEM + Builtin Administrators (default) */
    WT_IPC_ACL_ADMIN_ONLY      /* same allow list + explicit deny Everyone/Network */
} WT_IpcAclMode;

WT_Result wt_service_ipc_acl_from_name(const wchar_t *name, WT_IpcAclMode *out);
const char *wt_service_ipc_acl_name(WT_IpcAclMode mode);
const char *wt_service_ipc_acl_describe(WT_IpcAclMode mode);
const wchar_t *wt_service_ipc_acl_sddl(WT_IpcAclMode mode);

WT_Result wt_service_ipc_acl_path(wchar_t *out, size_t count);
WT_Result wt_service_ipc_acl_load(WT_IpcAclMode *out);
WT_Result wt_service_ipc_acl_save(WT_IpcAclMode mode);
WT_Result wt_service_ipc_acl_remove(void);

/* User-facing explanation when pipe CreateFile/WaitNamedPipe is denied. */
void wt_service_ipc_print_access_denied(FILE *out);

/* Sends a UTF-8 JSON request and receives a UTF-8 JSON payload.
 * Caller must free(*response_out) with free().
 * Returns WT_ERR_ACCESS_DENIED when the pipe ACL rejects the caller. */
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
