#ifndef WINTUNE_COMMANDS_SERVICES_H
#define WINTUNE_COMMANDS_SERVICES_H

#include "cli/cli.h"

/* `wintune services` - lists Win32 services with state, start type and PID.
 * Read-only. Filters (--auto/--running/--stopped/--failed) are OR-combined. */
int wt_cmd_services(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_SERVICES_H */
