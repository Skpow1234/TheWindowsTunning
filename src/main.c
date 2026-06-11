#include "cli/cli.h"

#include <windows.h>
#include <locale.h>

/* Wide entry point so command-line arguments arrive as UTF-16, matching the
 * wide-character Windows APIs used throughout WinTune. */
int wmain(int argc, wchar_t **argv)
{
    /* Emit UTF-8 for both redirected output (via the CRT locale) and the live
     * console (via the output code page), so non-ASCII names/paths are not
     * mangled. This keeps text output consistent with the UTF-8 JSON output. */
    setlocale(LC_ALL, ".UTF-8");
    SetConsoleOutputCP(CP_UTF8);

    return wt_cli_run(argc, argv);
}
