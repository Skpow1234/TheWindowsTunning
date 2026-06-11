#ifndef WINTUNE_COMMANDS_POWER_H
#define WINTUNE_COMMANDS_POWER_H

#include "cli/cli.h"

/* `wintune power`            -> show current plan, source, recommendation.
 * `wintune power --set <p>`  -> switch to an existing scheme (balanced |
 *                               performance | saver | ultimate) with
 *                               confirmation and rollback metadata. */
int wt_cmd_power(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_POWER_H */
