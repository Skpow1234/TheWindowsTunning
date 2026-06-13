#include "cli/cli_launcher.h"

#include "cli/cli.h"
#include "cli/exit_codes.h"
#include "platform/console.h"

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
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
    printf("  -> wintune %ls\n", buf);
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

static void wt_cli_print_menu_row(int color, const char *num, const char *cmd,
                                  const char *desc)
{
    const char *rst = color ? "\x1b[0m" : "";
    const char *bold = color ? "\x1b[1m" : "";
    const char *dim = color ? "\x1b[2m" : "";
    const char *acc = color ? "\x1b[96m" : "";

    if (color) {
        printf("    %s%s%s  %s%-14s%s %s%s\n", bold, num, rst, acc, cmd, rst,
               dim, desc);
    } else {
        printf("    %s  %-14s %s\n", num, cmd, desc);
    }
}

static void wt_cli_print_launcher_banner(int color)
{
    const char *rst = color ? "\x1b[0m" : "";
    const char *bold = color ? "\x1b[1m" : "";
    const char *cyan = color ? "\x1b[36m" : "";
    const char *dim = color ? "\x1b[2m" : "";

    printf("\n");
    if (color) {
        printf("%s+-----------------------------------------------------------------------------+%s\n",
               dim, rst);
        printf("%s|%s %sWinTune %s%s - Interactive mode%s                              %s|%s\n",
               dim, rst, bold, WT_VERSION_STRING, cyan, rst, dim, rst);
        printf("%s|%s  Native Windows performance diagnostics.%s                         %s|%s\n",
               dim, rst, dim, dim, rst);
        printf("%s+-----------------------------------------------------------------------------+%s\n\n",
               dim, rst);
    } else {
        printf("  WinTune %s - Interactive mode\n", WT_VERSION_STRING);
        printf("  Native Windows performance diagnostics.\n\n");
    }

    printf("%s  Quick picks (type a number or any full command):%s\n\n", bold, rst);
    wt_cli_print_menu_row(color, "1", "doctor", "scan + recommendations + summary");
    wt_cli_print_menu_row(color, "2", "scan", "full performance scan");
    wt_cli_print_menu_row(color, "3", "top", "process usage snapshot");
    wt_cli_print_menu_row(color, "4", "startup", "startup entries");
    wt_cli_print_menu_row(color, "5", "power", "power plan + tips");
    wt_cli_print_menu_row(color, "6", "recommend", "recommendations only");
    wt_cli_print_menu_row(color, "7", "help", "all commands and options");
    wt_cli_print_menu_row(color, "8", "tui", "live dashboard (best visuals)");
    wt_cli_print_menu_row(color, "9", "rollback list", "saved rollback records");

    printf("\n%s  Examples:%s\n", bold, rst);
    printf("%s    scan --samples 3%s\n", dim, rst);
    printf("%s    top --watch%s\n", dim, rst);
    printf("%s    apply WT-POWER-001 --yes%s\n", dim, rst);
    printf("%s    startup --include-tasks%s\n\n", dim, rst);
    printf("%s  quit | exit | q%s - leave interactive mode\n\n", dim, rst);

    if (color) {
        printf("%s  Tip: use Windows Terminal for the best colors and fonts.%s\n\n",
               dim, rst);
    }
}

static void wt_cli_print_prompt(int color)
{
    if (color) {
        printf("\x1b[1m\x1b[36mwintune>\x1b[0m ");
    } else {
        printf("wintune> ");
    }
    fflush(stdout);
}

int wt_cli_interactive_launcher(void)
{
    (void)wt_console_enable_vt();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int color = wt_console_supports_color();
    wt_cli_print_launcher_banner(color);

    wchar_t line[1024];
    wchar_t mapped[256];

    for (;;) {
        wt_cli_print_prompt(color);

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

    printf("\nGoodbye.\n");
    return WT_EXIT_OK;
}
