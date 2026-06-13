#include "platform/service_client.h"

#include "platform/service_ipc.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wt_service_client_extract_message(const char *resp, char *msg,
                                              size_t msg_cap)
{
    if (resp == NULL || msg == NULL || msg_cap == 0) {
        return;
    }
    const char *msg_key = strstr(resp, "\"message\"");
    if (msg_key == NULL) {
        return;
    }
    const char *q = strchr(msg_key, ':');
    if (q == NULL) {
        return;
    }
    q = strchr(q, '"');
    if (q == NULL) {
        return;
    }
    q++;
    const char *q2 = strchr(q, '"');
    if (q2 == NULL) {
        return;
    }
    size_t n = (size_t)(q2 - q);
    if (n >= msg_cap) {
        n = msg_cap - 1;
    }
    memcpy(msg, q, n);
    msg[n] = '\0';
}

static WT_Result wt_service_client_action(const char *req_json, char *msg,
                                          size_t msg_cap)
{
    char *resp = NULL;
    size_t len = 0;
    WT_Result r = wt_service_ipc_call(req_json, &resp, &len, 60000);
    if (r != WT_OK) {
        return r;
    }

    wt_service_client_extract_message(resp, msg, msg_cap);
    int ok = (resp != NULL && strstr(resp, "\"ok\":true") != NULL);
    free(resp);
    return ok ? WT_OK : WT_ERR_UNKNOWN;
}

static int wt_service_client_utf8_from_w(const wchar_t *w, char *out,
                                         size_t out_cap)
{
    if (w == NULL || out == NULL || out_cap == 0) {
        return 0;
    }
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)out_cap, NULL,
                               NULL) > 0;
}

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

WT_Result wt_service_client_scan(int doctor_mode, long interval_ms,
                                 int text_format, FILE *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[160];
    const char *cmd = doctor_mode ? "doctor" : "scan";
    if (interval_ms > 0 && text_format) {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"%s\",\"interval_ms\":%ld,\"format\":\"text\"}",
                 cmd, interval_ms);
    } else if (interval_ms > 0) {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"%s\",\"interval_ms\":%ld}", cmd, interval_ms);
    } else if (text_format) {
        snprintf(req, sizeof(req), "{\"cmd\":\"%s\",\"format\":\"text\"}", cmd);
    } else {
        snprintf(req, sizeof(req), "{\"cmd\":\"%s\"}", cmd);
    }

    char *resp = NULL;
    size_t len = 0;
    WT_Result r = wt_service_ipc_call(req, &resp, &len, 60000);
    if (r != WT_OK) {
        return r;
    }

    fwrite(resp, 1, len, out);
    if (len == 0 || resp[len - 1] != '\n') {
        fputc('\n', out);
    }
    free(resp);
    return WT_OK;
}

WT_Result wt_service_client_apply(const wchar_t *id, const wchar_t *target_id,
                                  unsigned long delay_seconds, int assume_yes,
                                  char *msg, size_t msg_cap)
{
    if (id == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char id_utf8[128];
    if (!wt_service_client_utf8_from_w(id, id_utf8, sizeof(id_utf8))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char target_utf8[256] = {0};
    if (target_id != NULL && target_id[0] != L'\0') {
        if (!wt_service_client_utf8_from_w(target_id, target_utf8,
                                           sizeof(target_utf8))) {
            return WT_ERR_INVALID_ARGUMENT;
        }
    }

    char req[512];
    if (target_utf8[0] != '\0' && delay_seconds > 0) {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"apply\",\"id\":\"%s\",\"target_id\":\"%s\","
                 "\"delay_seconds\":%lu,\"yes\":%d}",
                 id_utf8, target_utf8, delay_seconds, assume_yes ? 1 : 0);
    } else if (target_utf8[0] != '\0') {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"apply\",\"id\":\"%s\",\"target_id\":\"%s\","
                 "\"yes\":%d}",
                 id_utf8, target_utf8, assume_yes ? 1 : 0);
    } else if (delay_seconds > 0) {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"apply\",\"id\":\"%s\",\"delay_seconds\":%lu,"
                 "\"yes\":%d}",
                 id_utf8, delay_seconds, assume_yes ? 1 : 0);
    } else {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"apply\",\"id\":\"%s\",\"yes\":%d}",
                 id_utf8, assume_yes ? 1 : 0);
    }
    return wt_service_client_action(req, msg, msg_cap);
}

WT_Result wt_service_client_power_set(const wchar_t *plan_token, int assume_yes,
                                      char *msg, size_t msg_cap)
{
    if (plan_token == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char plan_utf8[64];
    if (!wt_service_client_utf8_from_w(plan_token, plan_utf8, sizeof(plan_utf8))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[256];
    snprintf(req, sizeof(req),
             "{\"cmd\":\"power_set\",\"plan\":\"%s\",\"yes\":%d}",
             plan_utf8, assume_yes ? 1 : 0);
    return wt_service_client_action(req, msg, msg_cap);
}

WT_Result wt_service_client_restart_service(const wchar_t *name, int assume_yes,
                                            char *msg, size_t msg_cap)
{
    if (name == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char name_utf8[256];
    if (!wt_service_client_utf8_from_w(name, name_utf8, sizeof(name_utf8))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[384];
    snprintf(req, sizeof(req),
             "{\"cmd\":\"restart_service\",\"name\":\"%s\",\"yes\":%d}",
             name_utf8, assume_yes ? 1 : 0);
    return wt_service_client_action(req, msg, msg_cap);
}

WT_Result wt_service_client_startup_set(const wchar_t *id, int enable,
                                        int assume_yes,
                                        char *msg, size_t msg_cap)
{
    if (id == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char id_utf8[256];
    if (!wt_service_client_utf8_from_w(id, id_utf8, sizeof(id_utf8))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[384];
    snprintf(req, sizeof(req),
             "{\"cmd\":\"startup_set\",\"id\":\"%s\",\"enable\":%d,\"yes\":%d}",
             id_utf8, enable ? 1 : 0, assume_yes ? 1 : 0);
    return wt_service_client_action(req, msg, msg_cap);
}

WT_Result wt_service_client_startup_delay(const wchar_t *id,
                                          unsigned long delay_seconds,
                                          int assume_yes,
                                          char *msg, size_t msg_cap)
{
    if (id == NULL || delay_seconds == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char id_utf8[256];
    if (!wt_service_client_utf8_from_w(id, id_utf8, sizeof(id_utf8))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[384];
    snprintf(req, sizeof(req),
             "{\"cmd\":\"startup_delay\",\"id\":\"%s\",\"delay_seconds\":%lu,"
             "\"yes\":%d}",
             id_utf8, delay_seconds, assume_yes ? 1 : 0);
    return wt_service_client_action(req, msg, msg_cap);
}

WT_Result wt_service_client_task_set(const wchar_t *id, int enable,
                                     unsigned long delay_seconds,
                                     int assume_yes,
                                     char *msg, size_t msg_cap)
{
    if (id == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char id_utf8[256];
    if (!wt_service_client_utf8_from_w(id, id_utf8, sizeof(id_utf8))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    char req[384];
    if (delay_seconds > 0) {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"task_set\",\"id\":\"%s\",\"delay_seconds\":%lu,"
                 "\"yes\":%d}",
                 id_utf8, delay_seconds, assume_yes ? 1 : 0);
    } else {
        snprintf(req, sizeof(req),
                 "{\"cmd\":\"task_set\",\"id\":\"%s\",\"enable\":%d,\"yes\":%d}",
                 id_utf8, enable ? 1 : 0, assume_yes ? 1 : 0);
    }
    return wt_service_client_action(req, msg, msg_cap);
}
