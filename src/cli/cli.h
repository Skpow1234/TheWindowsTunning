#ifndef WINTUNE_CLI_H
#define WINTUNE_CLI_H

#include <wchar.h> /* wchar_t is a typedef in C, not a built-in keyword */

#define WT_VERSION_STRING "0.1.0"

/* Parsed options shared by every command. Command-specific flags (e.g.
 * --limit, --watch) are parsed here too and simply ignored by commands that
 * do not use them. Numeric fields default to -1 to mean "not specified". */
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

    /* Command-specific options. */
    int watch;                  /* --watch */
    int no_recommendations;     /* --no-recommendations */
    long limit;                 /* --limit N         (-1 = default) */
    long interval_ms;           /* --interval N      (-1 = default) */
    long samples;               /* --samples N       (-1 = default) */
    const wchar_t *sort;        /* --sort <key>      (NULL = default) */
} WT_CliOptions;

/* Parses arguments, applies global options, and dispatches to a command.
 * Returns a process exit code (0 == success). */
int wt_cli_run(int argc, wchar_t **argv);

#endif /* WINTUNE_CLI_H */
