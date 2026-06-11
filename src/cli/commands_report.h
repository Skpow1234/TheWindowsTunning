#ifndef WINTUNE_COMMANDS_REPORT_H
#define WINTUNE_COMMANDS_REPORT_H

#include "cli/cli.h"

/* `wintune report` writes a local performance report (text or JSON).
 *   --format text|json   (default text; --json implies json)
 *   --output <path>      write to file instead of stdout */
int wt_cmd_report(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_REPORT_H */
