#ifndef WINTUNE_JSON_ERROR_H
#define WINTUNE_JSON_ERROR_H

#include <stdio.h>

#include "common/error.h"

struct WT_CliOptions;

/* Emits a machine-readable error document (schema_version + error object). */
void wt_json_emit_error(FILE *out,
                        const struct WT_CliOptions *opts,
                        const wchar_t *command,
                        WT_Result result,
                        const char *message,
                        int exit_code);

#endif /* WINTUNE_JSON_ERROR_H */
