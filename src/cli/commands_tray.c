#include "cli/commands_tray.h"

#include "tray/tray.h"
#include "cli/exit_codes.h"

int wt_cmd_tray(const WT_CliOptions *opts)
{
    (void)opts;
    WT_Result r = wt_tray_run();
    return (r == WT_OK) ? WT_EXIT_OK : WT_EXIT_ERROR;
}
