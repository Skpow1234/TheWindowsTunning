#include "cli/commands_apply.h"
#include "actions/apply.h"
#include "actions/apply_preview.h"
#include "cli/cli.h"
#include "cli/cli_exit.h"
#include "platform/service_client.h"
#include "platform/service_ipc.h"

#include <stdio.h>

int wt_cmd_apply(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "wintune: apply requires a recommendation id\n"
                "Usage: wintune apply <id> [target-id] [--seconds N] [--dry-run]\n"
                "  wintune apply WT-POWER-001\n"
                "  wintune apply WT-POWER-001 --dry-run\n"
                "  wintune apply WT-STARTUP-DISABLE \"HKCU\\\\Run:App\"\n"
                "  wintune apply WT-STARTUP-DELAY \"HKCU\\\\Run:App\" --seconds 30\n"
                "See current ids with 'wintune recommend'.\n");
        return wt_cli_exit_usage(opts, L"apply",
                                 "apply requires a recommendation id");
    }

    unsigned long delay = 0;
    if (opts->delay_seconds > 0) {
        delay = (unsigned long)opts->delay_seconds;
    }

    if (opts->dry_run) {
        WT_ApplyRequest req = {
            .rec_id = opts->arg1,
            .target_id = opts->arg2,
            .delay_seconds = delay,
            .assume_yes = 0,
            .msg = NULL,
            .msg_cap = 0,
        };
        WT_ApplyPreview preview;
        WT_Result r = wt_apply_preview_from_request(&req, &preview);
        if (wt_cli_is_json_mode(opts)) {
            wt_apply_preview_print_json(stdout, &preview);
        } else {
            wt_apply_preview_print_text(stdout, &preview);
        }
        return wt_cli_exit_from_result(opts, r, L"apply",
                                       preview.summary[0] != '\0'
                                           ? preview.summary
                                           : NULL);
    }

    char msg[512] = {0};
    WT_Result r;

    if (wt_cli_should_route_via_service(opts)) {
        WT_Result pr = wt_service_client_ping(2000);
        if (pr != WT_OK) {
            if (pr == WT_ERR_ACCESS_DENIED) {
                wt_service_ipc_print_access_denied(stderr);
            }
            return wt_cli_exit_from_result(
                opts, pr, L"apply", wt_service_client_ipc_error_message(pr));
        }
        r = wt_service_client_apply(opts->arg1, opts->arg2, delay, opts->yes,
                                    msg, sizeof(msg));
        if (r == WT_ERR_ACCESS_DENIED) {
            wt_service_ipc_print_access_denied(stderr);
        }
    } else {
        r = wt_apply_recommendation(opts->arg1, opts->arg2, delay, opts->yes,
                                    msg, sizeof(msg));
    }

    if (msg[0] != '\0' && !wt_cli_is_json_mode(opts)) {
        printf("%s\n", msg);
    }
    return wt_cli_exit_from_result(opts, r, L"apply",
                                   msg[0] != '\0' ? msg : NULL);
}
