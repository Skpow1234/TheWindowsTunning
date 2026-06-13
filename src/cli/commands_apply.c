#include "cli/commands_apply.h"
#include "actions/apply.h"
#include "cli/cli.h"
#include "platform/service_client.h"

#include <stdio.h>

int wt_cmd_apply(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "wintune: apply requires a recommendation id\n"
                "Usage: wintune apply <id> [target-id] [--seconds N]\n"
                "  wintune apply WT-POWER-001\n"
                "  wintune apply WT-STARTUP-DISABLE \"HKCU\\\\Run:App\"\n"
                "  wintune apply WT-STARTUP-DELAY \"HKCU\\\\Run:App\" --seconds 30\n"
                "See current ids with 'wintune recommend'.\n");
        return 2;
    }

    char msg[512] = {0};
    WT_Result r;
    unsigned long delay = 0;
    if (opts->delay_seconds > 0) {
        delay = (unsigned long)opts->delay_seconds;
    }

    if (wt_cli_should_route_via_service(opts)) {
        if (!wt_service_client_is_available(2000)) {
            fprintf(stderr,
                    "wintune: WinTune service is not reachable for apply.\n");
            return 1;
        }
        r = wt_service_client_apply(opts->arg1, opts->arg2, delay, opts->yes,
                                    msg, sizeof(msg));
    } else {
        r = wt_apply_recommendation(opts->arg1, opts->arg2, delay, opts->yes,
                                    msg, sizeof(msg));
    }

    if (msg[0] != '\0') {
        printf("%s\n", msg);
    }
    return (r == WT_OK) ? 0 : 1;
}
