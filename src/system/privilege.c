#include "system/privilege.h"

#include <windows.h>
#include <stdio.h>

#include "platform/console.h"

int wt_is_process_elevated(void)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return 0;
    }

    TOKEN_ELEVATION elevation;
    DWORD size = sizeof(elevation);
    int elevated = 0;
    if (GetTokenInformation(token, TokenElevation, &elevation,
                            sizeof(elevation), &size)) {
        elevated = (elevation.TokenIsElevated != 0) ? 1 : 0;
    }

    CloseHandle(token);
    return elevated;
}

void wt_print_admin_required_message(FILE *out)
{
    if (out == NULL) {
        return;
    }

    fputs("This action requires administrator privileges.\n", out);
    if (wt_session_is_remote()) {
        fputs("Over SSH, run from an elevated PowerShell or CMD session "
              "(not a normal user shell).\n", out);
        fputs("Example: start an elevated shell first, then:\n"
              "  wintune <command>\n", out);
        fputs("Optional: install the WinTune Windows Service once on the host "
              "so privileged actions can use --via-service without UAC over SSH.\n",
              out);
    } else {
        fputs("Right-click PowerShell or CMD → Run as administrator, then retry.\n",
              out);
        fputs("Read-only commands (scan, doctor, top, tui) usually work without "
              "elevation.\n", out);
        fputs("Optional: wintune service install  (elevated, once) for "
              "background scans and --via-service.\n", out);
    }
}
