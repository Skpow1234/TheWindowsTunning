#ifndef WINTUNE_COMMANDS_SCAN_H
#define WINTUNE_COMMANDS_SCAN_H

#include "cli/cli.h"

/* Implements `wintune scan`. Returns a process exit code. */
int wt_cmd_scan(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_SCAN_H */
