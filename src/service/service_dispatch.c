#include "service/service.h"

#include "cli/cli.h"
#include "core/scan.h"
#include "core/recommendations.h"
#include "actions/apply.h"
#include "actions/safe_actions.h"
#include "output/json.h"
#include "output/text.h"
#include "system/power.h"
#include "system/tasks.h"
#include "platform/paths.h"
#include "platform/time.h"
#include "system/privilege.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define WT_SERVICE_SCAN_INTERVAL_MS (15u * 60u * 1000u)

static int wt_json_extract_cmd(const char *json, char *cmd_out, size_t cmd_cap)
{
    if (json == NULL || cmd_out == NULL || cmd_cap == 0) {
        return 0;
    }
    const char *key = strstr(json, "\"cmd\"");
    if (key == NULL) {
        return 0;
    }
    const char *colon = strchr(key, ':');
    if (colon == NULL) {
        return 0;
    }
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) {
        return 0;
    }
    q1++;
    const char *q2 = strchr(q1, '"');
    if (q2 == NULL || (size_t)(q2 - q1) >= cmd_cap) {
        return 0;
    }
    memcpy(cmd_out, q1, (size_t)(q2 - q1));
    cmd_out[q2 - q1] = '\0';
    return 1;
}

static int wt_json_extract_int(const char *json, const char *field, long *out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    const char *key = strstr(json, pattern);
    if (key == NULL) {
        return 0;
    }
    const char *colon = strchr(key + strlen(pattern), ':');
    if (colon == NULL) {
        return 0;
    }
    *out = strtol(colon + 1, NULL, 10);
    return 1;
}

static char *wt_json_extract_string_field(const char *json, const char *field)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    const char *key = strstr(json, pattern);
    if (key == NULL) {
        return NULL;
    }
    const char *colon = strchr(key + strlen(pattern), ':');
    if (colon == NULL) {
        return NULL;
    }
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) {
        return NULL;
    }
    q1++;
    const char *q2 = strchr(q1, '"');
    if (q2 == NULL) {
        return NULL;
    }
    size_t len = (size_t)(q2 - q1);
    char *out = (char *)malloc(len + 1u);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, q1, len);
    out[len] = '\0';
    return out;
}

static WT_Result wt_service_write_json_to_buffer(
    void (*writer)(const void *ctx, FILE *out), const void *ctx,
    char **out_buf, size_t *out_len)
{
    FILE *tmp = tmpfile();
    if (tmp == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    writer(ctx, tmp);
    fflush(tmp);

    if (fseek(tmp, 0, SEEK_END) != 0) {
        fclose(tmp);
        return WT_ERR_UNKNOWN;
    }
    long sz = ftell(tmp);
    if (sz < 0) {
        fclose(tmp);
        return WT_ERR_UNKNOWN;
    }
    if (fseek(tmp, 0, SEEK_SET) != 0) {
        fclose(tmp);
        return WT_ERR_UNKNOWN;
    }

    char *buf = (char *)malloc((size_t)sz + 1u);
    if (buf == NULL) {
        fclose(tmp);
        return WT_ERR_OUT_OF_MEMORY;
    }

    size_t got = fread(buf, 1, (size_t)sz, tmp);
    fclose(tmp);
    buf[got] = '\0';
    *out_buf = buf;
    *out_len = got;
    return WT_OK;
}

typedef struct WT_ScanWriteCtx {
    WT_ScanReport report;
    WT_RecommendationList recs;
    int include_recs;
    int doctor_summary;
    int text_format;
} WT_ScanWriteCtx;

static void wt_service_scan_writer(const void *ctx, FILE *out)
{
    const WT_ScanWriteCtx *c = (const WT_ScanWriteCtx *)ctx;
    if (c->text_format) {
        wt_print_scan_report_text_to(
            out, &c->report, c->include_recs ? &c->recs : NULL);
        if (c->doctor_summary) {
            wt_print_doctor_summary_text(out, &c->recs);
        }
    } else {
        const WT_RecommendationList *recs_ptr =
            c->include_recs ? &c->recs : NULL;
        wt_print_scan_report_json(&c->report, recs_ptr, out);
    }
}

static int wt_json_wants_text_format(const char *json)
{
    if (json == NULL) {
        return 0;
    }
    return (strstr(json, "\"format\":\"text\"") != NULL ||
            strstr(json, "\"format\": \"text\"") != NULL);
}

static int wt_json_utf8_to_wchar(const char *utf8, wchar_t *out, size_t out_count)
{
    if (utf8 == NULL || out == NULL || out_count == 0) {
        return 0;
    }
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, (int)out_count) > 0;
}

static WT_Result wt_service_make_action_response(const char *cmd, WT_Result result,
                                                 const char *msg,
                                                 char **resp_out, size_t *resp_len)
{
    char buf[768];
    if (result == WT_OK) {
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"cmd\":\"%s\",\"message\":\"%s\"}",
                 cmd, msg != NULL && msg[0] != '\0' ? msg : "ok");
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"ok\":false,\"cmd\":\"%s\",\"error\":\"%s\","
                 "\"message\":\"%s\"}",
                 cmd, wt_result_to_string(result),
                 msg != NULL && msg[0] != '\0' ? msg : "");
    }
    size_t n = strlen(buf);
    char *copy = (char *)malloc(n + 1u);
    if (copy == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    memcpy(copy, buf, n + 1u);
    *resp_out = copy;
    *resp_len = n;
    return WT_OK;
}

WT_Result wt_service_run_scan_and_cache(void)
{
    WT_ScanOptions opts = {0};
    WT_ScanWriteCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    WT_Result r = wt_run_scan(&opts, &ctx.report);
    if (r != WT_OK) {
        return r;
    }
    wt_generate_recommendations(&ctx.report, &ctx.recs);
    ctx.include_recs = 1;

    char *json = NULL;
    size_t json_len = 0;
    r = wt_service_write_json_to_buffer(wt_service_scan_writer, &ctx, &json,
                                        &json_len);
    if (r != WT_OK) {
        return r;
    }

    wchar_t path[MAX_PATH];
    r = wt_paths_last_scan_file(path, ARRAYSIZE(path));
    if (r == WT_OK) {
        wchar_t dir[MAX_PATH];
        if (wt_paths_program_data_dir(dir, ARRAYSIZE(dir)) == WT_OK) {
            (void)wt_paths_ensure_dir(dir);
        }
        FILE *f = NULL;
        if (_wfopen_s(&f, path, L"wb") == 0 && f != NULL) {
            fwrite(json, 1, json_len, f);
            fclose(f);
        }
    }

    free(json);
    return WT_OK;
}

static WT_Result wt_service_make_error(const char *msg, char **resp_out,
                                       size_t *resp_len)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    size_t n = strlen(buf);
    char *copy = (char *)malloc(n + 1u);
    if (copy == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    memcpy(copy, buf, n + 1u);
    *resp_out = copy;
    *resp_len = n;
    return WT_OK;
}

WT_Result wt_service_handle_request(const char *request_json,
                                    char **resp_out,
                                    size_t *resp_len)
{
    if (request_json == NULL || resp_out == NULL || resp_len == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *resp_out = NULL;
    *resp_len = 0;

    char cmd[64];
    if (!wt_json_extract_cmd(request_json, cmd, sizeof(cmd))) {
        return wt_service_make_error("missing cmd", resp_out, resp_len);
    }

    if (strcmp(cmd, "ping") == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"cmd\":\"pong\",\"version\":\"%s\","
                 "\"elevated\":%s}",
                 WT_VERSION_STRING, wt_is_process_elevated() ? "true" : "false");
        size_t n = strlen(buf);
        char *copy = (char *)malloc(n + 1u);
        if (copy == NULL) {
            return WT_ERR_OUT_OF_MEMORY;
        }
        memcpy(copy, buf, n + 1u);
        *resp_out = copy;
        *resp_len = n;
        return WT_OK;
    }

    if (strcmp(cmd, "status") == 0) {
        char ts[32] = {0};
        (void)wt_now_iso8601_utc(ts, sizeof(ts));
        wchar_t scan_path[MAX_PATH];
        int has_scan = 0;
        if (wt_paths_last_scan_file(scan_path, ARRAYSIZE(scan_path)) == WT_OK &&
            GetFileAttributesW(scan_path) != INVALID_FILE_ATTRIBUTES) {
            has_scan = 1;
        }
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"cmd\":\"status\",\"running\":true,"
                 "\"elevated\":%s,\"last_scan_cached\":%s,"
                 "\"timestamp_utc\":\"%s\"}",
                 wt_is_process_elevated() ? "true" : "false",
                 has_scan ? "true" : "false",
                 ts[0] != '\0' ? ts : "");
        size_t n = strlen(buf);
        char *copy = (char *)malloc(n + 1u);
        if (copy == NULL) {
            return WT_ERR_OUT_OF_MEMORY;
        }
        memcpy(copy, buf, n + 1u);
        *resp_out = copy;
        *resp_len = n;
        return WT_OK;
    }

    if (strcmp(cmd, "scan") == 0 || strcmp(cmd, "doctor") == 0) {
        WT_ScanOptions scan_opts = {0};
        long interval = 0;
        if (wt_json_extract_int(request_json, "interval_ms", &interval) &&
            interval > 0) {
            scan_opts.cpu_sample_ms = (unsigned int)interval;
        }

        WT_ScanWriteCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        WT_Result r = wt_run_scan(&scan_opts, &ctx.report);
        if (r != WT_OK) {
            return wt_service_make_error("scan failed", resp_out, resp_len);
        }

        ctx.include_recs = (strcmp(cmd, "doctor") == 0) ? 1 : 0;
        ctx.doctor_summary = (strcmp(cmd, "doctor") == 0) ? 1 : 0;
        ctx.text_format = wt_json_wants_text_format(request_json) ? 1 : 0;
        if (ctx.include_recs) {
            wt_generate_recommendations(&ctx.report, &ctx.recs);
        }

        r = wt_service_write_json_to_buffer(wt_service_scan_writer, &ctx,
                                            resp_out, resp_len);
        if (r == WT_OK) {
            (void)wt_service_run_scan_and_cache();
        }
        return r;
    }

    if (strcmp(cmd, "power_set") == 0) {
        char *plan = wt_json_extract_string_field(request_json, "plan");
        if (plan == NULL) {
            return wt_service_make_error("power_set requires plan", resp_out,
                                         resp_len);
        }
        long yes = 0;
        (void)wt_json_extract_int(request_json, "yes", &yes);

        wchar_t wplan[64];
        if (!wt_json_utf8_to_wchar(plan, wplan, ARRAYSIZE(wplan))) {
            free(plan);
            return wt_service_make_error("invalid plan", resp_out, resp_len);
        }
        free(plan);

        WT_PowerScheme target = wt_power_scheme_from_token(wplan);
        if (target == WT_POWER_UNKNOWN) {
            return wt_service_make_error("unknown power plan", resp_out, resp_len);
        }

        char msg[512] = {0};
        WT_Result r = wt_action_set_power_plan(target, (int)yes, msg, sizeof(msg));
        return wt_service_make_action_response("power_set", r, msg, resp_out,
                                               resp_len);
    }

    if (strcmp(cmd, "restart_service") == 0) {
        char *name = wt_json_extract_string_field(request_json, "name");
        if (name == NULL) {
            return wt_service_make_error("restart_service requires name",
                                         resp_out, resp_len);
        }
        long yes = 0;
        (void)wt_json_extract_int(request_json, "yes", &yes);

        wchar_t wname[256];
        if (!wt_json_utf8_to_wchar(name, wname, ARRAYSIZE(wname))) {
            free(name);
            return wt_service_make_error("invalid name", resp_out, resp_len);
        }
        free(name);

        char msg[512] = {0};
        WT_Result r =
            wt_action_restart_service(wname, (int)yes, msg, sizeof(msg));
        return wt_service_make_action_response("restart_service", r, msg,
                                               resp_out, resp_len);
    }

    if (strcmp(cmd, "startup_set") == 0) {
        char *id = wt_json_extract_string_field(request_json, "id");
        if (id == NULL) {
            return wt_service_make_error("startup_set requires id", resp_out,
                                         resp_len);
        }
        long yes = 0;
        long enable = 1;
        (void)wt_json_extract_int(request_json, "yes", &yes);
        (void)wt_json_extract_int(request_json, "enable", &enable);

        wchar_t wid[256];
        if (!wt_json_utf8_to_wchar(id, wid, ARRAYSIZE(wid))) {
            free(id);
            return wt_service_make_error("invalid id", resp_out, resp_len);
        }
        free(id);

        char msg[512] = {0};
        WT_Result r = wt_action_set_startup_enabled(wid, enable != 0, (int)yes,
                                                    msg, sizeof(msg));
        return wt_service_make_action_response("startup_set", r, msg, resp_out,
                                               resp_len);
    }

    if (strcmp(cmd, "startup_delay") == 0) {
        char *id = wt_json_extract_string_field(request_json, "id");
        if (id == NULL) {
            return wt_service_make_error("startup_delay requires id", resp_out,
                                         resp_len);
        }
        long yes = 0;
        long delay = 0;
        (void)wt_json_extract_int(request_json, "yes", &yes);
        if (!wt_json_extract_int(request_json, "delay_seconds", &delay) ||
            delay <= 0) {
            free(id);
            return wt_service_make_error("startup_delay requires delay_seconds",
                                         resp_out, resp_len);
        }

        wchar_t wid[256];
        if (!wt_json_utf8_to_wchar(id, wid, ARRAYSIZE(wid))) {
            free(id);
            return wt_service_make_error("invalid id", resp_out, resp_len);
        }
        free(id);

        char msg[512] = {0};
        WT_Result r = wt_action_set_startup_delay(wid, (unsigned long)delay,
                                                  (int)yes, msg, sizeof(msg));
        return wt_service_make_action_response("startup_delay", r, msg,
                                               resp_out, resp_len);
    }

    if (strcmp(cmd, "task_set") == 0) {
        char *id = wt_json_extract_string_field(request_json, "id");
        if (id == NULL) {
            return wt_service_make_error("task_set requires id", resp_out,
                                         resp_len);
        }
        long yes = 0;
        long enable = 1;
        long delay = 0;
        (void)wt_json_extract_int(request_json, "yes", &yes);
        (void)wt_json_extract_int(request_json, "enable", &enable);
        int has_delay = wt_json_extract_int(request_json, "delay_seconds", &delay);

        wchar_t wid[256];
        if (!wt_json_utf8_to_wchar(id, wid, ARRAYSIZE(wid))) {
            free(id);
            return wt_service_make_error("invalid id", resp_out, resp_len);
        }
        free(id);

        char msg[512] = {0};
        WT_Result r;
        if (has_delay && delay > 0) {
            r = wt_action_set_task_delay(wid, (unsigned long)delay, (int)yes,
                                         msg, sizeof(msg));
        } else {
            r = wt_action_set_task_enabled(wid, enable != 0, (int)yes, msg,
                                           sizeof(msg));
        }
        return wt_service_make_action_response("task_set", r, msg, resp_out,
                                               resp_len);
    }

    if (strcmp(cmd, "apply") == 0) {
        char *id = wt_json_extract_string_field(request_json, "id");
        if (id == NULL) {
            return wt_service_make_error("apply requires id", resp_out, resp_len);
        }
        long yes = 0;
        (void)wt_json_extract_int(request_json, "yes", &yes);

        char msg[512] = {0};
        wchar_t wid[64];
        if (MultiByteToWideChar(CP_UTF8, 0, id, -1, wid, (int)ARRAYSIZE(wid)) <= 0) {
            free(id);
            return wt_service_make_error("invalid id", resp_out, resp_len);
        }
        free(id);

        WT_Result r = wt_apply_recommendation(wid, (int)yes, msg, sizeof(msg));
        return wt_service_make_action_response("apply", r, msg, resp_out,
                                               resp_len);
    }

    return wt_service_make_error("unknown cmd", resp_out, resp_len);
}

unsigned wt_service_scan_interval_ms(void)
{
    return WT_SERVICE_SCAN_INTERVAL_MS;
}
