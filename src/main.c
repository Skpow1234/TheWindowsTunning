#include "cli/cli.h"
#include "service/service.h"

#include <windows.h>
#include <locale.h>

/* Wide entry point so command-line arguments arrive as UTF-16, matching the
 * wide-character Windows APIs used throughout WinTune. */
int wmain(int argc, wchar_t **argv)
{
    /* SCM invokes: wintune.exe service run */
    if (argc >= 3 && wcscmp(argv[1], L"service") == 0 &&
        wcscmp(argv[2], L"run") == 0) {
        return wt_service_run_dispatcher();
    }

    /* Emit UTF-8 for both redirected output (via the CRT character locale) and
     * the live console (via the output code page), so non-ASCII names/paths are
     * not mangled. Only LC_CTYPE is changed: LC_NUMERIC stays "C" so decimal
     * separators remain '.' (required for valid JSON and stable text output). */
    setlocale(LC_CTYPE, ".UTF-8");
    SetConsoleOutputCP(CP_UTF8);

    return wt_cli_run(argc, argv);
}
