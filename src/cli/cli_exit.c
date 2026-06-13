#include "cli/cli_exit.h"

#include "cli/exit_codes.h"
#include "output/json_error.h"

#include <stdio.h>
#include <string.h>

static struct {
    WT_Result result;
    char message[512];
    int valid;
} g_cli_last_error;

void wt_cli_set_last_error(WT_Result result, const char *message)
{
    g_cli_last_error.result = result;
    g_cli_last_error.valid = 1;
    if (message != NULL) {
        strncpy_s(g_cli_last_error.message, sizeof(g_cli_last_error.message),
                  message, _TRUNCATE);
    } else {
        g_cli_last_error.message[0] = '\0';
    }
}

void wt_cli_clear_last_error(void)
{
    g_cli_last_error.valid = 0;
    g_cli_last_error.message[0] = '\0';
}

static void wt_cli_maybe_emit_json_error(const WT_CliOptions *opts,
                                         const wchar_t *command,
                                         WT_Result result,
                                         const char *message,
                                         int exit_code)
{
    if (opts == NULL || !opts->json_errors) {
        return;
    }

    FILE *out = (opts->json || wt_cli_is_json_mode(opts)) ? stdout : stderr;
    wt_json_emit_error(out, opts, command, result, message, exit_code);
}

int wt_cli_exit_from_result(const WT_CliOptions *opts,
                            WT_Result result,
                            const wchar_t *command,
                            const char *message)
{
    if (result == WT_OK) {
        wt_cli_clear_last_error();
        return WT_EXIT_OK;
    }

    const char *msg = message;
    if (msg == NULL || msg[0] == '\0') {
        if (g_cli_last_error.valid && g_cli_last_error.message[0] != '\0') {
            msg = g_cli_last_error.message;
        } else {
            msg = wt_result_to_string(result);
        }
    }

    wt_cli_set_last_error(result, msg);
    int code = wt_exit_code_from_result(result);
    wt_cli_maybe_emit_json_error(opts, command, result, msg, code);
    return code;
}

int wt_cli_finish(const WT_CliOptions *opts, int rc, const wchar_t *command)
{
    if (rc == WT_EXIT_OK) {
        wt_cli_clear_last_error();
        return WT_EXIT_OK;
    }

    if (rc == WT_EXIT_USAGE || rc == 2) {
        WT_Result r = WT_ERR_INVALID_ARGUMENT;
        const char *msg = g_cli_last_error.valid && g_cli_last_error.message[0] != '\0'
                              ? g_cli_last_error.message
                              : "invalid usage";
        wt_cli_maybe_emit_json_error(opts, command, r, msg, WT_EXIT_USAGE);
        return WT_EXIT_USAGE;
    }

    if (rc == WT_EXIT_NOT_IMPLEMENTED) {
        wt_cli_maybe_emit_json_error(opts, command, WT_ERR_NOT_SUPPORTED,
                                     "command not implemented yet",
                                     WT_EXIT_NOT_IMPLEMENTED);
        return WT_EXIT_NOT_IMPLEMENTED;
    }

    /* Specific exit codes from wt_cli_exit_from_result (already emitted). */
    if (rc != 1 && rc != WT_EXIT_ERROR) {
        return rc;
    }

    WT_Result result = WT_ERR_UNKNOWN;
    const char *msg = "command failed";
    if (g_cli_last_error.valid) {
        result = g_cli_last_error.result;
        if (g_cli_last_error.message[0] != '\0') {
            msg = g_cli_last_error.message;
        } else {
            msg = wt_result_to_string(result);
        }
    }

    int code = wt_exit_code_from_result(result);
    wt_cli_maybe_emit_json_error(opts, command, result, msg, code);
    return code;
}

int wt_cli_exit_usage(const WT_CliOptions *opts,
                      const wchar_t *command,
                      const char *message)
{
    wt_cli_set_last_error(WT_ERR_INVALID_ARGUMENT, message);
    wt_cli_maybe_emit_json_error(opts, command, WT_ERR_INVALID_ARGUMENT,
                                 message, WT_EXIT_USAGE);
    return WT_EXIT_USAGE;
}
