#ifndef WINTUNE_COMMANDS_RECOMMEND_H
#define WINTUNE_COMMANDS_RECOMMEND_H

#include "cli/cli.h"

/* `wintune recommend` - generates explainable recommendations without applying
 * any changes. Read-only. */
int wt_cmd_recommend(const WT_CliOptions *opts);

#endif /* WINTUNE_COMMANDS_RECOMMEND_H */
