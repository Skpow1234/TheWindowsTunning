#include "system/privilege.h"

#include <windows.h>

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
