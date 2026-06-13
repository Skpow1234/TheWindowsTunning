#include "platform/console.h"

#include <windows.h>
#include <tlhelp32.h>
#include <strsafe.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

static HANDLE wt_stdout_handle(void)
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

static HANDLE wt_stdin_handle(void)
{
    return GetStdHandle(STD_INPUT_HANDLE);
}

static int wt_handle_is_console(HANDLE h)
{
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) ? 1 : 0;
}

static int wt_env_is_set(const wchar_t *name)
{
    wchar_t buf[8];
    return GetEnvironmentVariableW(name, buf, ARRAYSIZE(buf)) > 0;
}

static WT_Result wt_console_write(const wchar_t *seq)
{
    HANDLE out = wt_stdout_handle();
    if (out == NULL || out == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    size_t len = 0;
    if (FAILED(StringCchLengthW(seq, STRSAFE_MAX_CCH, &len))) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    DWORD written = 0;
    if (!WriteConsoleW(out, seq, (DWORD)len, &written, NULL)) {
        return WT_ERR_WIN32;
    }
    return WT_OK;
}

WT_Result wt_console_enable_vt(void)
{
    HANDLE out = wt_stdout_handle();
    if (out == NULL || out == INVALID_HANDLE_VALUE) {
        return WT_ERR_WIN32;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(out, &mode)) {
        /* Not a console (redirected / piped). */
        return WT_ERR_NOT_SUPPORTED;
    }

    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        return WT_OK;
    }

    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        return WT_ERR_WIN32;
    }
    return WT_OK;
}

WT_Result wt_console_get_size(int *rows, int *cols)
{
    if (rows == NULL || cols == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    HANDLE out = wt_stdout_handle();
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info)) {
        return WT_ERR_NOT_SUPPORTED;
    }

    *cols = (int)(info.srWindow.Right - info.srWindow.Left + 1);
    *rows = (int)(info.srWindow.Bottom - info.srWindow.Top + 1);
    return WT_OK;
}

WT_Result wt_console_clear(void)
{
    return wt_console_write(L"\x1b[2J\x1b[H");
}

WT_Result wt_console_move_cursor(int row, int col)
{
    if (row < 1) row = 1;
    if (col < 1) col = 1;

    wchar_t seq[32];
    if (FAILED(StringCchPrintfW(seq, ARRAYSIZE(seq), L"\x1b[%d;%dH", row, col))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return wt_console_write(seq);
}

WT_Result wt_console_hide_cursor(void)
{
    return wt_console_write(L"\x1b[?25l");
}

WT_Result wt_console_show_cursor(void)
{
    return wt_console_write(L"\x1b[?25h");
}

int wt_console_is_interactive(void)
{
    return wt_handle_is_console(wt_stdout_handle());
}

int wt_console_stdin_is_interactive(void)
{
    /* _isatty covers console and some TTY-like handles on Windows. */
    return _isatty(_fileno(stdin)) ? 1 : 0;
}

int wt_session_is_interactive(void)
{
    return wt_console_is_interactive() && wt_console_stdin_is_interactive();
}

int wt_session_is_remote(void)
{
    /* Windows OpenSSH Server exports these for remote sessions. */
    return wt_env_is_set(L"SSH_CONNECTION") || wt_env_is_set(L"SSH_CLIENT");
}

int wt_console_supports_color(void)
{
    return wt_console_is_interactive();
}

static DWORD wt_console_parent_process_id(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    DWORD parent = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                parent = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return parent;
}

static int wt_console_process_name(DWORD pid, wchar_t *name, size_t name_count)
{
    if (name == NULL || name_count == 0) {
        return 0;
    }
    name[0] = L'\0';

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    int found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                StringCchCopyW(name, name_count, pe.szExeFile);
                found = 1;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

int wt_console_launched_from_explorer(void)
{
    DWORD parent_pid = wt_console_parent_process_id(GetCurrentProcessId());
    if (parent_pid == 0) {
        return 0;
    }

    wchar_t parent_name[MAX_PATH];
    if (!wt_console_process_name(parent_pid, parent_name, ARRAYSIZE(parent_name))) {
        return 0;
    }
    return (_wcsicmp(parent_name, L"explorer.exe") == 0) ? 1 : 0;
}

static int wt_console_runs_inside_known_shell(void)
{
    char buf[64];
    if (GetEnvironmentVariableA("MSYSTEM", buf, (DWORD)sizeof(buf)) > 0) {
        return 1;
    }
    if (GetEnvironmentVariableA("SHELL", buf, (DWORD)sizeof(buf)) > 0) {
        if (strstr(buf, "bash") != NULL || strstr(buf, "sh") != NULL) {
            return 1;
        }
    }
    return 0;
}

void wt_console_hold_open_if_explorer_launch(int argc)
{
    if (argc > 1) {
        return;
    }
    if (!wt_session_is_interactive() || wt_session_is_remote()) {
        return;
    }
    if (wt_console_runs_inside_known_shell()) {
        return;
    }
    if (!wt_console_launched_from_explorer()) {
        return;
    }

    static const wchar_t msg[] =
        L"\r\n"
        L"WinTune is a command-line tool — it does not stay open like a GUI app.\r\n"
        L"Open PowerShell or CMD in this folder and run, for example:\r\n"
        L"\r\n"
        L"  wintune doctor\r\n"
        L"  wintune scan\r\n"
        L"  wintune help\r\n"
        L"\r\n"
        L"Tip: double-click Run-Doctor.cmd for a quick health check.\r\n"
        L"\r\n"
        L"Press Enter to close this window...\r\n";

    HANDLE out = wt_stdout_handle();
    if (out != NULL && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        (void)WriteConsoleW(out, msg, (DWORD)(ARRAYSIZE(msg) - 1), &written, NULL);
    }

    HANDLE in = wt_stdin_handle();
    if (in == NULL || in == INVALID_HANDLE_VALUE) {
        return;
    }

    wchar_t ch[4];
    DWORD read = 0;
    (void)ReadConsoleW(in, ch, 1, &read, NULL);
}
