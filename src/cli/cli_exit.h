#ifndef WINTUNE_CLI_EXIT_H
#define WINTUNE_CLI_EXIT_H

#include <wchar.h>

#include "common/error.h"
#include "cli/cli.h"

/* Records the last failure for wt_cli_finish / --json-errors emission. */
void wt_cli_set_last_error(WT_Result result, const char *message);
void wt_cli_clear_last_error(void);

/* Maps WT_Result to a stable exit code; emits JSON error when --json-errors. */
int wt_cli_exit_from_result(const WT_CliOptions *opts,
                            WT_Result result,
                            const wchar_t *command,
                            const char *message);

/* Normalizes command return values (0/1/2) and emits JSON errors when set. */
int wt_cli_finish(const WT_CliOptions *opts, int rc, const wchar_t *command);

/* Usage failure with optional JSON error document. */
int wt_cli_exit_usage(const WT_CliOptions *opts,
                      const wchar_t *command,
                      const char *message);

#endif /* WINTUNE_CLI_EXIT_H */
