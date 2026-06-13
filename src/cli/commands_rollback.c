#include "cli/commands_rollback.h"
#include "actions/rollback.h"
#include "cli/cli.h"
#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "common/error.h"

#include <stdio.h>
#include <wchar.h>

int wt_cmd_rollback(const WT_CliOptions *opts)
{
    const wchar_t *sub = (opts != NULL) ? opts->arg1 : NULL;

    if (sub == NULL || wcscmp(sub, L"list") == 0) {
        if (opts != NULL && opts->json) {
            wt_cli_configure_json_output(opts);
        }
        WT_Result r = wt_rollback_list(stdout, (opts != NULL) ? opts->json : 0);
        return wt_cli_exit_from_result(opts, r, L"rollback", NULL);
    }

    if (wcscmp(sub, L"apply") == 0) {
        if (opts == NULL || opts->arg2 == NULL) {
            fprintf(stderr,
                    "wintune: rollback apply requires an id\n"
                    "Usage: wintune rollback apply <id>\n"
                    "List ids with 'wintune rollback list'.\n");
            return wt_cli_exit_usage(opts, L"rollback",
                                     "rollback apply requires an id");
        }
        WT_Result r = wt_rollback_apply(opts->arg2, opts->yes);
        if (r == WT_OK) {
            if (!wt_cli_is_json_mode(opts)) {
                wprintf(L"Rollback '%ls' applied.\n", opts->arg2);
            }
            return WT_EXIT_OK;
        }
        if (r == WT_ERR_CANCELLED) {
            return wt_cli_exit_from_result(opts, r, L"rollback",
                                           "Cancelled. Nothing was changed.");
        }
        if (r == WT_ERR_NOT_FOUND) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "No rollback record with id '%ls'.", opts->arg2);
            return wt_cli_exit_from_result(opts, r, L"rollback", msg);
        }
        return wt_cli_exit_from_result(opts, r, L"rollback",
                                       wt_result_to_string(r));
    }

    fwprintf(stderr,
             L"wintune: unknown rollback subcommand '%ls'. Use 'list' or 'apply'.\n",
             sub);
    return wt_cli_exit_usage(opts, L"rollback", "unknown rollback subcommand");
}
