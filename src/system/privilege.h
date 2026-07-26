#ifndef WINTUNE_PRIVILEGE_H
#define WINTUNE_PRIVILEGE_H

#include <stdio.h>

/* Returns 1 if the current process is running with an elevated (administrator)
 * token, 0 otherwise. On failure to query the token, returns 0 (not elevated).
 * Read-only; performs no privilege changes. */
int wt_is_process_elevated(void);

/* Prints a calm, actionable elevation hint to `out` (typically stderr). */
void wt_print_admin_required_message(FILE *out);

#endif /* WINTUNE_PRIVILEGE_H */
