#include "cli/commands_rollback.h"
#include "actions/rollback.h"
#include "common/error.h"

#include <stdio.h>
#include <wchar.h>

int wt_cmd_rollback(const WT_CliOptions *opts)
{
    const wchar_t *sub = (opts != NULL) ? opts->arg1 : NULL;

    /* Default action is "list" when no subcommand is given. */
    if (sub == NULL || wcscmp(sub, L"list") == 0) {
        WT_Result r = wt_rollback_list(stdout, (opts != NULL) ? opts->json : 0);
        return (r == WT_OK) ? 0 : 1;
    }

    if (wcscmp(sub, L"apply") == 0) {
        if (opts->arg2 == NULL) {
            fprintf(stderr,
                    "wintune: rollback apply requires an id\n"
                    "Usage: wintune rollback apply <id>\n"
                    "List ids with 'wintune rollback list'.\n");
            return 2;
        }
        WT_Result r = wt_rollback_apply(opts->arg2, opts->yes);
        if (r == WT_OK) {
            wprintf(L"Rollback '%ls' applied.\n", opts->arg2);
            return 0;
        }
        if (r == WT_ERR_CANCELLED) {
            printf("Cancelled. Nothing was changed.\n");
            return 1;
        }
        if (r == WT_ERR_NOT_FOUND) {
            fwprintf(stderr, L"wintune: no rollback record with id '%ls'\n", opts->arg2);
            return 1;
        }
        fprintf(stderr, "wintune: rollback failed (%s)\n", wt_result_to_string(r));
        return 1;
    }

    fwprintf(stderr,
             L"wintune: unknown rollback subcommand '%ls'. Use 'list' or 'apply'.\n",
             sub);
    return 2;
}
