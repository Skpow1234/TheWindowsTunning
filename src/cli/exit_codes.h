#ifndef WINTUNE_EXIT_CODES_H
#define WINTUNE_EXIT_CODES_H

#include "common/error.h"

/* Stable process exit codes for scripting and fleet automation.
 * Documented in docs/json-schema.md and docs/fleet.md. */

#define WT_EXIT_OK               0
#define WT_EXIT_USAGE            2
#define WT_EXIT_CANCELLED       10
#define WT_EXIT_ACCESS_DENIED   11
#define WT_EXIT_NOT_FOUND       12
#define WT_EXIT_NOT_SUPPORTED   13
#define WT_EXIT_TIMEOUT         14
#define WT_EXIT_ERROR           20
#define WT_EXIT_NOT_IMPLEMENTED 21

/* Maps a WT_Result to a stable exit code. */
int wt_exit_code_from_result(WT_Result result);

/* Stable snake_case name for JSON error payloads. Never NULL. */
const char *wt_result_code_name(WT_Result result);

/* Human-oriented exit code label (e.g. "access_denied"). Never NULL. */
const char *wt_exit_code_name(int exit_code);

#endif /* WINTUNE_EXIT_CODES_H */
