#include "cli/cli_launcher.h"

#include "cli/cli.h"
#include "cli/exit_codes.h"
#include "platform/console.h"

#include <windows.h>
#include <strsafe.h>
#include <string.h>
#include <wchar.h>

int wt_cli_should_show_launcher(int argc)
{
    if (argc > 1) {
        return 0;
    }
    if (!wt_session_is_interactive() || wt_session_is_remote()) {
        return 0;
    }
    return wt_console_launched_from_explorer();
}

static void wt_cli_trim_inplace(wchar_t *buf)
{
    if (buf == NULL || buf[0] == L'\0') {
        return;
    }
    wchar_t *start = buf;
    while (*start == L' ' || *start == L'\t') {
        ++start;
    }
    if (start != buf) {
        memmove(buf, start, (wcslen(start) + 1) * sizeof(wchar_t));
    }
    size_t len = wcslen(buf);
    while (len > 0 && (buf[len - 1] == L' ' || buf[len - 1] == L'\t' ||
                       buf[len - 1] == L'\r' || buf[len - 1] == L'\n')) {
        buf[--len] = L'\0';
    }
}

static int wt_cli_read_line(wchar_t *buf, size_t cap)
{
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = L'\0';

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in == NULL || in == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD read = 0;
    if (!ReadConsoleW(in, buf, (DWORD)(cap - 1), &read, NULL)) {
        return 0;
    }
    buf[read] = L'\0';
    wt_cli_trim_inplace(buf);
    return 1;
}

static int wt_cli_line_is_quit(const wchar_t *line)
{
    return line != NULL &&
           (_wcsicmp(line, L"q") == 0 || _wcsicmp(line, L"quit") == 0 ||
            _wcsicmp(line, L"exit") == 0);
}

static const wchar_t *wt_cli_menu_shortcut(const wchar_t *line, wchar_t *buf,
                                         size_t buf_cap)
{
    if (line == NULL || line[0] == L'\0') {
        return NULL;
    }
    if (wcslen(line) != 1 || line[0] < L'1' || line[0] > L'9') {
        return line;
    }

    static const wchar_t *shortcuts[] = {
        L"doctor",
        L"scan",
        L"top",
        L"startup",
        L"power",
        L"recommend",
        L"help",
        L"tui",
        L"rollback list",
    };
    size_t idx = (size_t)(line[0] - L'1');
    if (idx >= ARRAYSIZE(shortcuts)) {
        return line;
    }
    StringCchCopyW(buf, buf_cap, shortcuts[idx]);
    wprintf(L"  -> wintune %ls\n", buf);
    return buf;
}

static int wt_cli_spawn_command_line(const wchar_t *args)
{
    if (args == NULL || args[0] == L'\0') {
        return WT_EXIT_OK;
    }

    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe)) == 0) {
        fwprintf(stderr, L"wintune: could not resolve executable path.\n");
        return WT_EXIT_ERROR;
    }

    wchar_t cmdline[2048];
    if (FAILED(StringCchPrintfW(cmdline, ARRAYSIZE(cmdline), L"\"%s\" %s",
                                 exe, args))) {
        fwprintf(stderr, L"wintune: command line too long.\n");
        return WT_EXIT_ERROR;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si,
                       &pi)) {
        fwprintf(stderr, L"wintune: failed to run '%ls'.\n", args);
        return WT_EXIT_ERROR;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = (DWORD)WT_EXIT_ERROR;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

static void wt_cli_print_launcher_banner(void)
{
    wprintf(
        L"\n"
        L"  WinTune %hs — Interactive mode\n"
        L"  Native Windows performance diagnostics.\n"
        L"\n"
        L"  Quick picks (type a number or any full command):\n"
        L"    1  doctor          scan + recommendations + summary\n"
        L"    2  scan            full performance scan\n"
        L"    3  top             process usage snapshot\n"
        L"    4  startup         startup entries\n"
        L"    5  power           power plan + tips\n"
        L"    6  recommend       recommendations only\n"
        L"    7  help            all commands and options\n"
        L"    8  tui             live terminal dashboard\n"
        L"    9  rollback list   saved rollback records\n"
        L"\n"
        L"  Examples:\n"
        L"    scan --samples 3\n"
        L"    top --watch\n"
        L"    apply WT-POWER-001 --yes\n"
        L"    startup --include-tasks\n"
        L"\n"
        L"  quit | exit | q  — leave interactive mode\n"
        L"\n",
        WT_VERSION_STRING);
}

int wt_cli_interactive_launcher(void)
{
    (void)wt_console_enable_vt();

    wt_cli_print_launcher_banner();

    wchar_t line[1024];
    wchar_t mapped[256];

    for (;;) {
        wprintf(L"wintune> ");
        fflush(stdout);

        if (!wt_cli_read_line(line, ARRAYSIZE(line))) {
            break;
        }
        if (line[0] == L'\0') {
            continue;
        }
        if (wt_cli_line_is_quit(line)) {
            break;
        }

        const wchar_t *cmd = wt_cli_menu_shortcut(line, mapped, ARRAYSIZE(mapped));
        (void)wt_cli_spawn_command_line(cmd);
    }

    wprintf(L"\nGoodbye.\n");
    return WT_EXIT_OK;
}
