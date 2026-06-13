#include "platform/service_client.h"

#include "platform/service_ipc.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wt_service_client_is_available(unsigned timeout_ms)
{
    char *resp = NULL;
    size_t len = 0;
    WT_Result r = wt_service_ipc_call("{\"cmd\":\"ping\"}", &resp, &len,
                                      timeout_ms);
    if (r != WT_OK) {
        return 0;
    }
    int ok = (resp != NULL && strstr(resp, "\"ok\":true") != NULL);
    free(resp);
    return ok;
}

WT_Result wt_service_client_scan(int doctor_mode, long interval_ms, FILE *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[128];
    if (interval_ms > 0) {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"%s\",\"interval_ms\":%ld}",
                 doctor_mode ? "doctor" : "scan", interval_ms);
    } else {
        snprintf(req, sizeof(req), "{\"cmd\":\"%s\"}",
                 doctor_mode ? "doctor" : "scan");
    }

    char *resp = NULL;
    size_t len = 0;
    WT_Result r = wt_service_ipc_call(req, &resp, &len, 60000);
    if (r != WT_OK) {
        return r;
    }

    fwrite(resp, 1, len, out);
    fputc('\n', out);
    free(resp);
    return WT_OK;
}

WT_Result wt_service_client_apply(const wchar_t *id, int assume_yes,
                                  char *msg, size_t msg_cap)
{
    if (id == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char id_utf8[128];
    if (WideCharToMultiByte(CP_UTF8, 0, id, -1, id_utf8, (int)sizeof(id_utf8),
                            NULL, NULL) <= 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[256];
    snprintf(req, sizeof(req),
             "{\"cmd\":\"apply\",\"id\":\"%s\",\"yes\":%d}",
             id_utf8, assume_yes ? 1 : 0);

    char *resp = NULL;
    size_t len = 0;
    WT_Result r = wt_service_ipc_call(req, &resp, &len, 60000);
    if (r != WT_OK) {
        return r;
    }

    const char *msg_key = strstr(resp, "\"message\"");
    if (msg_key != NULL && msg != NULL && msg_cap > 0) {
        const char *q = strchr(msg_key, ':');
        if (q != NULL) {
            q = strchr(q, '"');
            if (q != NULL) {
                q++;
                const char *q2 = strchr(q, '"');
                if (q2 != NULL) {
                    size_t n = (size_t)(q2 - q);
                    if (n >= msg_cap) {
                        n = msg_cap - 1;
                    }
                    memcpy(msg, q, n);
                    msg[n] = '\0';
                }
            }
        }
    }

    int ok = (strstr(resp, "\"ok\":true") != NULL);
    free(resp);
    return ok ? WT_OK : WT_ERR_UNKNOWN;
}
