#ifndef WINTUNE_PRIVILEGE_H
#define WINTUNE_PRIVILEGE_H

/* Returns 1 if the current process is running with an elevated (administrator)
 * token, 0 otherwise. Never fails: on any error it conservatively reports 0
 * (not elevated). Read-only; performs no privilege changes. */
int wt_is_process_elevated(void);

#endif /* WINTUNE_PRIVILEGE_H */
