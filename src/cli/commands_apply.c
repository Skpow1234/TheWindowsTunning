#include "cli/commands_apply.h"
#include "actions/apply.h"

#include <stdio.h>

int wt_cmd_apply(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "wintune: apply requires a recommendation id\n"
                "Usage: wintune apply <id>   (e.g. WT-POWER-001)\n"
                "See current ids with 'wintune recommend'.\n");
        return 2;
    }

    char msg[512] = {0};
    WT_Result r = wt_apply_recommendation(opts->arg1, opts->yes, msg, sizeof(msg));
    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}
