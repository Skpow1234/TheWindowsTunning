#include "cli/cli.h"

#include <windows.h>
#include <locale.h>

/* Wide entry point so command-line arguments arrive as UTF-16, matching the
 * wide-character Windows APIs used throughout WinTune. */
int wmain(int argc, wchar_t **argv)
{
    /* Emit UTF-8 for both redirected output (via the CRT character locale) and
     * the live console (via the output code page), so non-ASCII names/paths are
     * not mangled. Only LC_CTYPE is changed: LC_NUMERIC stays "C" so decimal
     * separators remain '.' (required for valid JSON and stable text output). */
    setlocale(LC_CTYPE, ".UTF-8");
    SetConsoleOutputCP(CP_UTF8);

    return wt_cli_run(argc, argv);
}
