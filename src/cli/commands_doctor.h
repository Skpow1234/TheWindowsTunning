#ifndef WINTUNE_COMMANDS_DOCTOR_H
#define WINTUNE_COMMANDS_DOCTOR_H

#include "cli/cli.h"

/* `wintune doctor` - friendly all-in-one: scan + recommendations + summary.
 * Read-only; never applies changes. */
int wt_cmd_doctor(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_DOCTOR_H */
