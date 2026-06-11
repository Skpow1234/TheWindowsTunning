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
        fputs("Over SSH, run from an elevated PowerShell or CMD session.\n", out);
        fputs("Example: connect as an admin user, or start an elevated shell "
              "before running wintune.\n", out);
    } else {
        fputs("Run PowerShell or CMD as Administrator and try again.\n", out);
    }
    fputs("A future WinTune Service-based workflow may support privileged "
          "actions remotely without an interactive UAC prompt.\n", out);
}
