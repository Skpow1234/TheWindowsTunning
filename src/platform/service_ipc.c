#include "platform/service_ipc.h"
#include "platform/paths.h"
#include "common/log.h"

#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

typedef struct WT_IpcHeader {
    unsigned long magic;
    unsigned long length;
} WT_IpcHeader;

/* Default: owner/group Builtin Administrators; allow SYSTEM + BA.
 * Remote clients rejected via PIPE_REJECT_REMOTE_CLIENTS. */
static const wchar_t *const WT_IPC_SDDL_ADMIN =
    L"O:BAG:BAD:(A;;GA;;;SY)(A;;GA;;;BA)";

/* Admin-only: protected DACL + explicit deny for Everyone / Anonymous / Network. */
static const wchar_t *const WT_IPC_SDDL_ADMIN_ONLY =
    L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(D;;GA;;;WD)(D;;GA;;;AN)(D;;GA;;;NU)";

WT_Result wt_service_ipc_acl_from_name(const wchar_t *name, WT_IpcAclMode *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (name == NULL || name[0] == L'\0' ||
        _wcsicmp(name, L"admin") == 0 ||
        _wcsicmp(name, L"default") == 0 ||
        _wcsicmp(name, L"local-admin") == 0) {
        *out = WT_IPC_ACL_ADMIN;
        return WT_OK;
    }
    if (_wcsicmp(name, L"admin-only") == 0 ||
        _wcsicmp(name, L"adminonly") == 0 ||
        _wcsicmp(name, L"strict") == 0) {
        *out = WT_IPC_ACL_ADMIN_ONLY;
        return WT_OK;
    }
    return WT_ERR_NOT_FOUND;
}

const char *wt_service_ipc_acl_name(WT_IpcAclMode mode)
{
    return (mode == WT_IPC_ACL_ADMIN_ONLY) ? "admin-only" : "admin";
}

const char *wt_service_ipc_acl_describe(WT_IpcAclMode mode)
{
    if (mode == WT_IPC_ACL_ADMIN_ONLY) {
        return "SYSTEM + local Administrators only; deny Everyone/Network";
    }
    return "SYSTEM + local Administrators (default)";
}

const wchar_t *wt_service_ipc_acl_sddl(WT_IpcAclMode mode)
{
    return (mode == WT_IPC_ACL_ADMIN_ONLY) ? WT_IPC_SDDL_ADMIN_ONLY
                                           : WT_IPC_SDDL_ADMIN;
}

WT_Result wt_service_ipc_acl_path(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\pipe_acl.json", dir))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

WT_Result wt_service_ipc_acl_load(WT_IpcAclMode *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out = WT_IPC_ACL_ADMIN;

    wchar_t path[MAX_PATH];
    if (wt_service_ipc_acl_path(path, ARRAYSIZE(path)) != WT_OK) {
        return WT_OK;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return WT_OK;
    }

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return WT_OK;
    }
    char buf[256];
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1u, &read, NULL);
    CloseHandle(h);
    if (!ok || read == 0) {
        return WT_OK;
    }
    buf[read] = '\0';

    if (strstr(buf, "admin-only") != NULL || strstr(buf, "adminonly") != NULL ||
        strstr(buf, "strict") != NULL) {
        *out = WT_IPC_ACL_ADMIN_ONLY;
    } else {
        *out = WT_IPC_ACL_ADMIN;
    }
    return WT_OK;
}

WT_Result wt_service_ipc_acl_save(WT_IpcAclMode mode)
{
    wchar_t dir[MAX_PATH];
    WT_Result r = wt_paths_program_data_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    r = wt_paths_ensure_dir(dir);
    if (r != WT_OK) {
        return r;
    }

    wchar_t path[MAX_PATH];
    r = wt_service_ipc_acl_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\n"
             "  \"version\": 1,\n"
             "  \"mode\": \"%s\",\n"
             "  \"description\": \"%s\",\n"
             "  \"reject_remote\": true\n"
             "}\n",
             wt_service_ipc_acl_name(mode), wt_service_ipc_acl_describe(mode));

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, body, (DWORD)strlen(body), &written, NULL);
    CloseHandle(h);
    return ok ? WT_OK : WT_ERR_WIN32;
}

WT_Result wt_service_ipc_acl_remove(void)
{
    wchar_t path[MAX_PATH];
    WT_Result r = wt_service_ipc_acl_path(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        return r;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return WT_OK;
    }
    if (!DeleteFileW(path)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return WT_OK;
        }
        return WT_ERR_WIN32;
    }
    return WT_OK;
}

void wt_service_ipc_print_access_denied(FILE *out)
{
    if (out == NULL) {
        return;
    }
    fputs("Access to the WinTune named pipe was denied by the pipe ACL.\n", out);
    fputs("The service accepts only local SYSTEM / Administrators "
          "(and rejects remote clients).\n", out);
    fputs("Run an elevated shell, or use a process in the Administrators group.\n",
          out);
    fputs("Check mode: wintune service status   "
          "(pipe_acl: admin | admin-only)\n", out);
    fputs("Change mode (admin): wintune service set-pipe-acl admin\n", out);
}

static WT_Result wt_ipc_map_create_error(DWORD err)
{
    if (err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD) {
        return WT_ERR_ACCESS_DENIED;
    }
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY) {
        return (err == ERROR_FILE_NOT_FOUND) ? WT_ERR_NOT_FOUND : WT_ERR_TIMEOUT;
    }
    return WT_ERR_WIN32;
}

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
        return wt_ipc_map_create_error(err);
    }

    HANDLE pipe = CreateFileW(WT_IPC_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        return wt_ipc_map_create_error(GetLastError());
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

    WT_IpcAclMode mode = WT_IPC_ACL_ADMIN;
    (void)wt_service_ipc_acl_load(&mode);

    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            wt_service_ipc_acl_sddl(mode), SDDL_REVISION_1, &sd, NULL)) {
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
