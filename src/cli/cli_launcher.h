#ifndef WINTUNE_CLI_LAUNCHER_H
#define WINTUNE_CLI_LAUNCHER_H

/* Returns 1 when wintune should enter interactive launcher mode (e.g. double-
 * click from Explorer with no arguments). */
int wt_cli_should_show_launcher(int argc);

/* Interactive prompt loop: run any wintune command until the user quits. */
int wt_cli_interactive_launcher(void);

#endif /* WINTUNE_CLI_LAUNCHER_H */
