#ifndef WINTUNE_SAFE_ACTIONS_H
#define WINTUNE_SAFE_ACTIONS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "system/power.h"

/* Interactive yes/no confirmation for mutating actions.
 *  - assume_yes != 0            => returns 1 without prompting (for --yes).
 *  - non-interactive stdin      => returns 0 (refuse) and prints a hint.
 *  - otherwise prompts "[y/N]"  => returns 1 only on an explicit yes.
 * The prompt text should NOT include the [y/N] suffix; it is added here. */
int wt_action_confirm(const char *prompt, int assume_yes);

/* Switches the active power plan to `target` (an existing standard scheme).
 * Captures the previous scheme and writes a rollback record on success. If the
 * machine is already on `target`, reports that and makes no change.
 * `msg`/`msg_cap` receive a human-readable result line for the caller to print. */
WT_Result wt_action_set_power_plan(WT_PowerScheme target,
                                   int assume_yes,
                                   char *msg, size_t msg_cap);

/* Restarts a Windows service (stop, then start) after confirmation. Requires
 * elevation. Refuses a small denylist of critical/security services. */
WT_Result wt_action_restart_service(const wchar_t *name,
                                    int assume_yes,
                                    char *msg, size_t msg_cap);

/* Enables/disables a startup entry using the Windows "StartupApproved"
 * mechanism (the same flag Task Manager toggles). Never deletes the underlying
 * Run value or startup file, so the change is fully reversible. HKLM-scoped
 * entries require elevation. Writes a rollback record on success. */
WT_Result wt_action_set_startup_enabled(const wchar_t *id,
                                        int enable,
                                        int assume_yes,
                                        char *msg, size_t msg_cap);

/* Writes the 12-byte StartupApproved flag for an entry. hive_tag is L"HKCU" or
 * L"HKLM". `enabled` != 0 writes the enabled flag, otherwise the disabled flag.
 * Exposed so rollback can restore a previous state without duplicating logic. */
WT_Result wt_startup_write_approved(const wchar_t *hive_tag,
                                    const wchar_t *subkey,
                                    const wchar_t *value_name,
                                    int enabled);

/* Writes delayed-start StartupApproved flag (Task Manager compatible). */
WT_Result wt_startup_write_delayed(const wchar_t *hive_tag,
                                   const wchar_t *subkey,
                                   const wchar_t *value_name,
                                   unsigned long delay_seconds);

WT_Result wt_action_set_startup_delay(const wchar_t *id,
                                      unsigned long delay_seconds,
                                      int assume_yes,
                                      char *msg, size_t msg_cap);

WT_Result wt_action_set_task_enabled(const wchar_t *id,
                                     int enable,
                                     int assume_yes,
                                     char *msg, size_t msg_cap);

WT_Result wt_action_set_task_delay(const wchar_t *id,
                                   unsigned long delay_seconds,
                                   int assume_yes,
                                   char *msg, size_t msg_cap);

#endif /* WINTUNE_SAFE_ACTIONS_H */
