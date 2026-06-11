#ifndef WINTUNE_COMMANDS_STARTUP_H
#define WINTUNE_COMMANDS_STARTUP_H

#include "cli/cli.h"

/* `wintune startup` - lists startup entries (registry Run/RunOnce + Startup
 * folders) and an estimated impact. Read-only. */
int wt_cmd_startup(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_STARTUP_H */
