#ifndef WINTUNE_COMMANDS_TOP_H
#define WINTUNE_COMMANDS_TOP_H

#include "cli/cli.h"

/* Implements `wintune top` (single snapshot in Phase 1). Returns an exit code. */
int wt_cmd_top(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_TOP_H */
