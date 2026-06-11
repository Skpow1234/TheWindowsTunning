#include "cli/cli.h"

/* Wide entry point so command-line arguments arrive as UTF-16, matching the
 * wide-character Windows APIs used throughout WinTune. */
int wmain(int argc, wchar_t **argv)
{
    return wt_cli_run(argc, argv);
}
