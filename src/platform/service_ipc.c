#include "platform/service_ipc.h"
#include "common/log.h"

#include <windows.h>
#include <sddl.h>
#include <stdlib.h>
#include <string.h>

typedef struct WT_IpcHeader {
    unsigned long magic;
    unsigned long length;
} WT_IpcHeader;

static WT_Result wt_ipc_write_all(HANDLE h, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    while (len > 0) {
        DWORD wrote = 0;
        if (!WriteFile(h, p, (DWORD)len, &wrote, NULL) || wrote == 0) {
            return WT_ERR_WIN32;
        }
        p += wrote;
        len -= wrote;
    }
    return WT_OK;
}

static WT_Result wt_ipc_read_all(HANDLE h, void *data, size_t len)
{
    unsigned char *p = (unsigned char *)data;
    while (len > 0) {
        DWORD got = 0;
        if (!ReadFile(h, p, (DWORD)len, &got, NULL)) {
            return WT_ERR_WIN32;
        }
        if (got == 0) {
            return WT_ERR_TIMEOUT;
        }
        p += got;
        len -= got;
    }
    return WT_OK;
}

WT_Result wt_service_ipc_call(const char *request_json,
                              char **response_out,
                              size_t *response_len,
                              unsigned timeout_ms)
{
    if (request_json == NULL || response_out == NULL || response_len == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *response_out = NULL;
    *response_len = 0;

    if (timeout_ms == 0) {
        timeout_ms = 30000;
    }

    if (!WaitNamedPipeW(WT_IPC_PIPE_NAME, timeout_ms)) {
        DWORD err = GetLastError();
        WT_LOGD("WaitNamedPipe failed (err=%lu)", err);
        return (err == ERROR_FILE_NOT_FOUND) ? WT_ERR_NOT_FOUND : WT_ERR_WIN32;
    }

    HANDLE pipe = CreateFileW(WT_IPC_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    (void)SetNamedPipeHandleState(pipe, &mode, NULL, NULL);

    size_t req_len = strlen(request_json);
    if (req_len > 1024 * 1024) {
        CloseHandle(pipe);
        return WT_ERR_INVALID_ARGUMENT;
    }

    WT_IpcHeader hdr;
    hdr.magic = WT_IPC_MAGIC;
    hdr.length = (unsigned long)req_len;

    WT_Result r = wt_ipc_write_all(pipe, &hdr, sizeof(hdr));
    if (r == WT_OK) {
        r = wt_ipc_write_all(pipe, request_json, req_len);
    }
    if (r != WT_OK) {
        CloseHandle(pipe);
        return r;
    }

    WT_IpcHeader rhdr;
    r = wt_ipc_read_all(pipe, &rhdr, sizeof(rhdr));
    if (r != WT_OK) {
        CloseHandle(pipe);
        return r;
    }
    if (rhdr.magic != WT_IPC_MAGIC || rhdr.length == 0 ||
        rhdr.length > 16u * 1024u * 1024u) {
        CloseHandle(pipe);
        return WT_ERR_UNKNOWN;
    }

    char *body = (char *)malloc((size_t)rhdr.length + 1u);
    if (body == NULL) {
        CloseHandle(pipe);
        return WT_ERR_OUT_OF_MEMORY;
    }

    r = wt_ipc_read_all(pipe, body, rhdr.length);
    CloseHandle(pipe);
    if (r != WT_OK) {
        free(body);
        return r;
    }

    body[rhdr.length] = '\0';
    *response_out = body;
    *response_len = rhdr.length;
    return WT_OK;
}

WT_Result wt_service_ipc_send_response(HANDLE pipe, const char *response_json,
                                       size_t response_len)
{
    if (pipe == INVALID_HANDLE_VALUE || response_json == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (response_len == 0) {
        response_len = strlen(response_json);
    }
    if (response_len > 16u * 1024u * 1024u) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    WT_IpcHeader hdr;
    hdr.magic = WT_IPC_MAGIC;
    hdr.length = (unsigned long)response_len;

    WT_Result r = wt_ipc_write_all(pipe, &hdr, sizeof(hdr));
    if (r == WT_OK) {
        r = wt_ipc_write_all(pipe, response_json, response_len);
    }
    return r;
}

WT_Result wt_service_ipc_read_request(HANDLE pipe, char **request_out,
                                      size_t *request_len)
{
    if (pipe == INVALID_HANDLE_VALUE || request_out == NULL || request_len == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *request_out = NULL;
    *request_len = 0;

    WT_IpcHeader hdr;
    WT_Result r = wt_ipc_read_all(pipe, &hdr, sizeof(hdr));
    if (r != WT_OK) {
        return r;
    }
    if (hdr.magic != WT_IPC_MAGIC || hdr.length == 0 ||
        hdr.length > 1024u * 1024u) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char *body = (char *)malloc((size_t)hdr.length + 1u);
    if (body == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    r = wt_ipc_read_all(pipe, body, hdr.length);
    if (r != WT_OK) {
        free(body);
        return r;
    }
    body[hdr.length] = '\0';
    *request_out = body;
    *request_len = hdr.length;
    return WT_OK;
}

WT_Result wt_service_ipc_create_pipe_instance(HANDLE *out_pipe)
{
    if (out_pipe == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_pipe = INVALID_HANDLE_VALUE;

    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    /* Local Administrators + SYSTEM only. Reject remote clients. */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd,
            NULL)) {
        return WT_ERR_WIN32;
    }
    sa.lpSecurityDescriptor = sd;

    HANDLE h = CreateNamedPipeW(
        WT_IPC_PIPE_NAME, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa);
    LocalFree(sd);

    if (h == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    *out_pipe = h;
    return WT_OK;
}
