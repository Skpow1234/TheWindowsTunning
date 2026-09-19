#ifndef WINTUNE_COMMANDS_APPS_H
#define WINTUNE_COMMANDS_APPS_H

#include "cli/cli.h"

/* `wintune apps` — uninstall advisor (read-only). Lists ARP metadata and
 * high-impact leftover candidates; never uninstalls software. */
int wt_cmd_apps(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_APPS_H */
