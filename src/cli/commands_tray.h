#ifndef WINTUNE_COMMANDS_TRAY_H
#define WINTUNE_COMMANDS_TRAY_H

#include "cli/cli.h"

/* `wintune tray` - native Win32 system tray (read-only by default). */
int wt_cmd_tray(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_TRAY_H */
