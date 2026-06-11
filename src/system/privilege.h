#ifndef WINTUNE_PRIVILEGE_H
#define WINTUNE_PRIVILEGE_H

#include <stdio.h>

/* Returns 1 if the current process is running with an elevated (administrator)
 * token, 0 otherwise. Never fails: on any error it conservatively reports 0
 * (not elevated). Read-only; performs no privilege changes. */
/* Returns 1 if the current process is running with an elevated (administrator)
 * token, 0 otherwise. Never fails: on any error it conservatively reports 0
 * (not elevated). Read-only; performs no privilege changes. */
int wt_is_process_elevated(void);

/* Prints the standard admin-required guidance to `out` (typically stderr),
 * with an SSH-specific hint when wt_session_is_remote() is true. */
void wt_print_admin_required_message(FILE *out);

#endif /* WINTUNE_PRIVILEGE_H */
