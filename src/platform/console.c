#include "platform/console.h"

#include <windows.h>
#include <strsafe.h>
#include <io.h>

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
