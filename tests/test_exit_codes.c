#include "cli/exit_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect_int(int got, int want, const char *label)
{
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", label, got, want);
        g_failed = 1;
    }
}

static void expect_str(const char *got, const char *want, const char *label)
{
    if (got == NULL || want == NULL || strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s: got '%s' want '%s'\n",
                label, got ? got : "(null)", want ? want : "(null)");
        g_failed = 1;
    }
}

int main(void)
{
    expect_int(wt_exit_code_from_result(WT_OK), WT_EXIT_OK, "ok");
    expect_int(wt_exit_code_from_result(WT_ERR_INVALID_ARGUMENT), WT_EXIT_USAGE,
               "invalid_argument");
    expect_int(wt_exit_code_from_result(WT_ERR_CANCELLED), WT_EXIT_CANCELLED,
               "cancelled");
    expect_int(wt_exit_code_from_result(WT_ERR_ACCESS_DENIED),
               WT_EXIT_ACCESS_DENIED, "access_denied");
    expect_int(wt_exit_code_from_result(WT_ERR_NOT_FOUND), WT_EXIT_NOT_FOUND,
               "not_found");
    expect_int(wt_exit_code_from_result(WT_ERR_NOT_SUPPORTED),
               WT_EXIT_NOT_SUPPORTED, "not_supported");
    expect_int(wt_exit_code_from_result(WT_ERR_TIMEOUT), WT_EXIT_TIMEOUT,
               "timeout");
    expect_int(wt_exit_code_from_result(WT_ERR_WIN32), WT_EXIT_ERROR, "win32");
    expect_int(wt_exit_code_from_result(WT_ERR_UNKNOWN), WT_EXIT_ERROR, "unknown");

    expect_str(wt_result_code_name(WT_ERR_ACCESS_DENIED), "access_denied",
               "result code name");
    expect_str(wt_exit_code_name(WT_EXIT_CANCELLED), "cancelled",
               "exit code name");

    if (g_failed) {
        fputs("exit_codes tests failed\n", stderr);
        return 1;
    }
    fputs("exit_codes tests passed\n", stdout);
    return 0;
}
