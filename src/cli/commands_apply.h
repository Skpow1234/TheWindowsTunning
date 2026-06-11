#ifndef WINTUNE_COMMANDS_APPLY_H
#define WINTUNE_COMMANDS_APPLY_H

#include "cli/cli.h"

/* `wintune apply <id>` applies the safe action for a recommendation id.
 * Mutating; honors --yes for confirmation. */
int wt_cmd_apply(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_APPLY_H */
