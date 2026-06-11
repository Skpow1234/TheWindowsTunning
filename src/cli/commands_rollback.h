#ifndef WINTUNE_COMMANDS_ROLLBACK_H
#define WINTUNE_COMMANDS_ROLLBACK_H

#include "cli/cli.h"

/* `wintune rollback list`        -> list saved rollback records.
 * `wintune rollback apply <id>`  -> undo the recorded action (with confirm). */
int wt_cmd_rollback(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_ROLLBACK_H */
