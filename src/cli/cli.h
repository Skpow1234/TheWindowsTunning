#ifndef WINTUNE_CLI_H
#define WINTUNE_CLI_H

#define WT_VERSION_STRING "0.1.0"

/* Parsed global options shared by every command. */
typedef struct WT_CliOptions {
    int help;
    int version;
    int verbose;
    int debug;
    int json;
    int no_color;
    int no_unicode;
    int safe_terminal;
    int yes;
    const wchar_t *output_path; /* NULL unless --output <path> was given */
} WT_CliOptions;

/* Parses arguments, applies global options, and dispatches to a command.
 * Returns a process exit code (0 == success). */
int wt_cli_run(int argc, wchar_t **argv);

#endif /* WINTUNE_CLI_H */
