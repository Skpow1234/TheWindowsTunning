#include "tray/tray.h"
#include "tray/tray_status.h"

#include "common/log.h"
#include "platform/paths.h"

#include <shellapi.h>
#include <stdio.h>
#include <strsafe.h>

#define WT_TRAY_WINDOW_CLASS L"WinTuneTrayHost"
#define WT_TRAY_CALLBACK_MSG (WM_USER + 1)

#define IDM_TRAY_STATUS      1001
#define IDM_TRAY_DOCTOR      1002
#define IDM_TRAY_REPORT      1003
#define IDM_TRAY_OPEN_LAST   1004
#define IDM_TRAY_OPEN_FOLDER 1005
#define IDM_TRAY_CLI         1006
#define IDM_TRAY_TUI         1007
#define IDM_TRAY_ABOUT       1009
#define IDM_TRAY_EXIT        1008

#define WT_TRAY_SINGLETON_MUTEX L"Global\\WinTuneTray_v1"

typedef struct WT_TrayState {
    NOTIFYICONDATAW nid;
    HMENU menu;
    HICON icon;
    HWND status_wnd;
} WT_TrayState;

static WT_TrayState g_tray;

static void wt_tray_get_exe_path(wchar_t *out, size_t count)
{
    if (out == NULL || count == 0) {
        return;
    }
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)count);
    if (n == 0 || n >= count) {
        out[0] = L'\0';
    }
}

static void wt_tray_get_exe_dir(wchar_t *out, size_t count)
{
    wt_tray_get_exe_path(out, count);
    if (out[0] == L'\0') {
        return;
    }
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash != NULL) {
        *slash = L'\0';
    }
}

static int wt_tray_spawn_wintune(const wchar_t *args, int wait_for_exit)
{
    wchar_t exe[MAX_PATH];
    wt_tray_get_exe_path(exe, ARRAYSIZE(exe));
    if (exe[0] == L'\0') {
        return 0;
    }

    wchar_t cmdline[1024];
    if (args != NULL && args[0] != L'\0') {
        if (FAILED(StringCchPrintfW(cmdline, ARRAYSIZE(cmdline), L"\"%s\" %s",
                                   exe, args))) {
            return 0;
        }
    } else {
        if (FAILED(StringCchCopyW(cmdline, ARRAYSIZE(cmdline), exe))) {
            return 0;
        }
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(exe, cmdline, NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
                        NULL, NULL, &si, &pi)) {
        WT_LOGW("CreateProcess failed (err=%lu)", GetLastError());
        return 0;
    }

    if (wait_for_exit) {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}

static void wt_tray_shell_open(const wchar_t *path)
{
    if (path == NULL || path[0] == L'\0') {
        return;
    }
    HINSTANCE r = ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOW);
    if ((INT_PTR)r <= 32) {
        WT_LOGW("ShellExecute open failed for %ls", path);
    }
}

static void wt_tray_shell_explore(const wchar_t *dir)
{
    if (dir == NULL || dir[0] == L'\0') {
        return;
    }
    HINSTANCE r = ShellExecuteW(NULL, L"explore", dir, NULL, NULL, SW_SHOW);
    if ((INT_PTR)r <= 32) {
        WT_LOGW("ShellExecute explore failed for %ls", dir);
    }
}

static void wt_tray_run_doctor(void)
{
    (void)wt_tray_spawn_wintune(L"doctor", 0);
}

static void wt_tray_run_report(void)
{
    wchar_t dir[MAX_PATH];
    if (wt_paths_reports_dir(dir, ARRAYSIZE(dir)) != WT_OK) {
        MessageBoxW(NULL, L"Could not resolve the reports folder.",
                    L"WinTune", MB_OK | MB_ICONWARNING);
        return;
    }
    (void)wt_paths_ensure_dir(dir);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    if (FAILED(StringCchPrintfW(
            path, ARRAYSIZE(path),
            L"%s\\wintune-report-%04u%02u%02u-%02u%02u%02u.txt", dir,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond))) {
        return;
    }

    wchar_t args[512];
    if (FAILED(StringCchPrintfW(args, ARRAYSIZE(args),
                                L"report --format text --output \"%s\"",
                                path))) {
        return;
    }

    if (!wt_tray_spawn_wintune(args, 1)) {
        MessageBoxW(NULL, L"Could not run wintune report.",
                    L"WinTune", MB_OK | MB_ICONERROR);
        return;
    }
    wt_tray_shell_open(path);
}

static void wt_tray_open_last_report(void)
{
    wchar_t path[MAX_PATH];
    WT_Result r = wt_paths_newest_report(path, ARRAYSIZE(path));
    if (r != WT_OK) {
        r = wt_paths_last_scan_file(path, ARRAYSIZE(path));
    }
    if (r != WT_OK || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(
            NULL,
            L"No report found yet.\r\n\r\n"
            L"Use \"Run report\" or \"Run doctor\" from the tray menu first.",
            L"WinTune", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wt_tray_shell_open(path);
}

static void wt_tray_open_reports_folder(void)
{
    wchar_t dir[MAX_PATH];
    if (wt_paths_reports_dir(dir, ARRAYSIZE(dir)) != WT_OK) {
        return;
    }
    (void)wt_paths_ensure_dir(dir);
    wt_tray_shell_explore(dir);
}

static void wt_tray_open_cli_launcher(void)
{
    wchar_t dir[MAX_PATH];
    wt_tray_get_exe_dir(dir, ARRAYSIZE(dir));
    wchar_t launcher[MAX_PATH];
    if (FAILED(StringCchPrintfW(launcher, ARRAYSIZE(launcher),
                                L"%s\\Launch-WinTune.cmd", dir))) {
        wt_tray_spawn_wintune(L"", 0);
        return;
    }
    if (GetFileAttributesW(launcher) != INVALID_FILE_ATTRIBUTES) {
        wt_tray_shell_open(launcher);
    } else {
        wchar_t cmdline[MAX_PATH + 32];
        if (SUCCEEDED(StringCchPrintfW(cmdline, ARRAYSIZE(cmdline),
                                       L"/k \"%s\\wintune.exe\"",
                                       dir))) {
            ShellExecuteW(NULL, L"open", L"cmd.exe", cmdline, dir, SW_SHOW);
        }
    }
}

static void wt_tray_run_tui(void)
{
    (void)wt_tray_spawn_wintune(L"tui", 0);
}

static void wt_tray_show_about(void)
{
    MessageBoxW(
        NULL,
        L"WinTune\n"
        L"Native Windows performance diagnostics.\n\n"
        L"The tray app is read-only by default.\n"
        L"System changes always require confirmation in the CLI.\n\n"
        L"No Electron. No WebView.",
        L"About WinTune",
        MB_OK | MB_ICONINFORMATION);
}

static void wt_tray_show_status(void)
{
    if (g_tray.status_wnd != NULL && IsWindow(g_tray.status_wnd)) {
        SetForegroundWindow(g_tray.status_wnd);
        return;
    }
    g_tray.status_wnd = wt_tray_status_show(NULL);
}

static void wt_tray_show_context_menu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(g_tray.menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN |
                                    TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
}

static void wt_tray_handle_menu(UINT id)
{
    switch (id) {
    case IDM_TRAY_STATUS:
        wt_tray_show_status();
        break;
    case IDM_TRAY_DOCTOR:
        wt_tray_run_doctor();
        break;
    case IDM_TRAY_REPORT:
        wt_tray_run_report();
        break;
    case IDM_TRAY_OPEN_LAST:
        wt_tray_open_last_report();
        break;
    case IDM_TRAY_OPEN_FOLDER:
        wt_tray_open_reports_folder();
        break;
    case IDM_TRAY_CLI:
        wt_tray_open_cli_launcher();
        break;
    case IDM_TRAY_TUI:
        wt_tray_run_tui();
        break;
    case IDM_TRAY_ABOUT:
        wt_tray_show_about();
        break;
    case IDM_TRAY_EXIT:
        PostQuitMessage(0);
        break;
    default:
        break;
    }
}

static void wt_tray_remove_icon(void)
{
    if (g_tray.nid.hWnd != NULL) {
        g_tray.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        Shell_NotifyIconW(NIM_DELETE, &g_tray.nid);
    }
    if (g_tray.menu != NULL) {
        DestroyMenu(g_tray.menu);
        g_tray.menu = NULL;
    }
    if (g_tray.icon != NULL) {
        DestroyIcon(g_tray.icon);
        g_tray.icon = NULL;
    }
}

static int wt_tray_add_icon(HWND hwnd)
{
    ZeroMemory(&g_tray.nid, sizeof(g_tray.nid));
    g_tray.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_tray.nid.hWnd = hwnd;
    g_tray.nid.uID = 1;
    g_tray.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.nid.uCallbackMessage = WT_TRAY_CALLBACK_MSG;
    g_tray.icon = LoadIconW(NULL, IDI_APPLICATION);
    g_tray.nid.hIcon = g_tray.icon;
    StringCchCopyW(g_tray.nid.szTip, ARRAYSIZE(g_tray.nid.szTip),
                   L"WinTune - performance diagnostics");

    if (!Shell_NotifyIconW(NIM_ADD, &g_tray.nid)) {
        return 0;
    }

    g_tray.menu = CreatePopupMenu();
    if (g_tray.menu == NULL) {
        return 0;
    }

    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_STATUS, L"Show status");
    AppendMenuW(g_tray.menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_DOCTOR, L"Run doctor...");
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_REPORT, L"Run report...");
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_OPEN_LAST, L"Open last report");
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_OPEN_FOLDER,
              L"Open reports folder");
    AppendMenuW(g_tray.menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_CLI, L"Open CLI menu...");
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_TUI, L"Live dashboard (TUI)...");
    AppendMenuW(g_tray.menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_ABOUT, L"About WinTune");
    AppendMenuW(g_tray.menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
    return 1;
}

static LRESULT CALLBACK wt_tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam,
                                           LPARAM lparam)
{
    switch (msg) {
    case WT_TRAY_CALLBACK_MSG:
        if (lparam == WM_LBUTTONDBLCLK) {
            wt_tray_show_status();
            return 0;
        }
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            wt_tray_show_context_menu(hwnd);
            return 0;
        }
        break;
    case WM_COMMAND:
        wt_tray_handle_menu(LOWORD(wparam));
        return 0;
    case WM_DESTROY:
        wt_tray_remove_icon();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int wt_tray_acquire_singleton(void)
{
    HANDLE h = CreateMutexW(NULL, FALSE, WT_TRAY_SINGLETON_MUTEX);
    if (h == NULL) {
        return 0;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        MessageBoxW(
            NULL,
            L"WinTune tray is already running.\r\n\r\n"
            L"Look for the WinTune icon in the notification area.",
            L"WinTune", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    return 1;
}

WT_Result wt_tray_run(void)
{
    (void)FreeConsole();

    if (!wt_tray_acquire_singleton()) {
        return WT_OK;
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wt_tray_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = WT_TRAY_WINDOW_CLASS;
    if (RegisterClassExW(&wc) == 0) {
        return WT_ERR_WIN32;
    }

    HWND hwnd = CreateWindowExW(0, WT_TRAY_WINDOW_CLASS, L"WinTune Tray",
                                0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                wc.hInstance, NULL);
    if (hwnd == NULL) {
        return WT_ERR_WIN32;
    }

    if (!wt_tray_add_icon(hwnd)) {
        DestroyWindow(hwnd);
        return WT_ERR_WIN32;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return WT_OK;
}
