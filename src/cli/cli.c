#include "cli/cli.h"

#include "cli/commands_scan.h"
#include "cli/commands_top.h"
#include "cli/commands_recommend.h"
#include "cli/commands_doctor.h"
#include "cli/commands_startup.h"
#include "cli/commands_services.h"
#include "cli/commands_power.h"
#include "cli/commands_apply.h"
#include "cli/commands_rollback.h"
#include "cli/commands_report.h"
#include "cli/commands_boot.h"
#include "tui/tui.h"
#include "common/error.h"
#include "common/log.h"
#include "platform/console.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdarg.h>

/* Exit codes kept small and stable for scripting. */
#define WT_EXIT_OK              0
#define WT_EXIT_NOT_IMPLEMENTED 1
#define WT_EXIT_USAGE           2

static void wt_print_usage(void)
{
    printf(
        "WinTune %s - native Windows performance diagnostics\n"
        "\n"
        "Usage:\n"
        "  wintune <command> [options]\n"
        "\n"
        "Commands:\n"
        "  scan        Run a full local performance scan\n"
        "  top         Show process usage (use --watch to refresh)\n"
        "  tui         Live terminal dashboard\n"
        "  startup     Show startup entries and estimated impact\n"
        "  boot        Boot/login performance analysis (ETW-backed)\n"
        "  services    Show service status and startup type\n"
        "  power       Show current power plan and recommendations\n"
        "  recommend   Generate recommendations without applying them\n"
        "  apply       Apply a specific recommendation\n"
        "  report      Write a local performance report\n"
        "  doctor      scan + recommend + summary (best for normal users)\n"
        "  rollback    List or apply rollback records\n"
        "  version     Show version information\n"
        "  help        Show this help\n"
        "\n"
        "Global options:\n"
        "  --help            Show help\n"
        "  --version         Show version\n"
        "  --verbose         Verbose (INFO) logging\n"
        "  --debug           Debug logging\n"
        "  --json            Machine-readable JSON output\n"
        "  --no-color        Disable ANSI colors\n"
        "  --no-unicode      ASCII fallback rendering\n"
        "  --safe-terminal   Conservative rendering for SSH/unknown terminals\n"
        "  --output <path>   Write output to a file\n"
        "  --yes             Confirm mutating actions (dangerous actions stay blocked)\n",
        WT_VERSION_STRING);
}

static void wt_print_version(void)
{
#if defined(NDEBUG)
    const char *build = "Release";
#else
    const char *build = "Debug";
#endif

#if defined(_MSC_VER)
    const char *compiler = "MSVC";
#elif defined(__clang__)
    const char *compiler = "clang";
#elif defined(__GNUC__)
    const char *compiler = "GCC/MinGW";
#else
    const char *compiler = "unknown";
#endif

#if defined(_M_X64) || defined(__x86_64__)
    const char *arch = "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    const char *arch = "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    const char *arch = "x86";
#else
    const char *arch = "unknown";
#endif

    printf("WinTune %s\n", WT_VERSION_STRING);
    printf("Build: %s\n", build);
    printf("Compiler: %s\n", compiler);
    printf("Arch: %s\n", arch);
}

/* Recognized commands that are defined but not yet implemented in this phase. */
static int wt_command_is_known(const wchar_t *cmd)
{
    static const wchar_t *known[] = {
        L"scan", L"top", L"tui", L"startup", L"boot", L"services", L"power",
        L"recommend", L"apply", L"report", L"doctor", L"rollback"
    };
    const size_t known_count = sizeof(known) / sizeof(known[0]);
    for (size_t i = 0; i < known_count; ++i) {
        if (wcscmp(cmd, known[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void wt_apply_log_level(const WT_CliOptions *opts)
{
    if (opts->debug) {
        wt_log_set_level(WT_LOG_DEBUG);
    } else if (opts->verbose) {
        wt_log_set_level(WT_LOG_INFO);
    } else {
        wt_log_set_level(WT_LOG_WARN);
    }
}

int wt_cli_is_json_mode(const WT_CliOptions *opts)
{
    if (opts == NULL) {
        return 0;
    }
    if (opts->json) {
        return 1;
    }
    if (opts->format != NULL && _wcsicmp(opts->format, L"json") == 0) {
        return 1;
    }
    return 0;
}

void wt_cli_apply_session_defaults(WT_CliOptions *opts)
{
    if (opts == NULL) {
        return;
    }

    if (wt_cli_is_json_mode(opts)) {
        opts->no_color = 1;
    }

    if (opts->safe_terminal) {
        opts->no_color = 1;
        opts->no_unicode = 1;
    }

    /* Piped/SSH one-shot sessions must not emit ANSI escape sequences on stdout. */
    if (!wt_console_is_interactive()) {
        opts->no_color = 1;
    }

    /* Remote interactive SSH (PTY) still benefits from conservative rendering
     * unless the user explicitly disabled safe-terminal semantics by forcing
     * unicode/color — only auto-enable when nothing was specified. */
    if (wt_session_is_remote() && wt_session_is_interactive() &&
            !opts->safe_terminal && !opts->no_unicode && !opts->no_color) {
        opts->safe_terminal = 1;
        opts->no_color = 1;
        opts->no_unicode = 1;
    }
}

void wt_cli_user_note(const WT_CliOptions *opts, const char *fmt, ...)
{
    if (fmt == NULL || wt_cli_is_json_mode(opts)) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

int wt_cli_run(int argc, wchar_t **argv)
{
    WT_CliOptions opts = {0};
    opts.limit = -1;
    opts.interval_ms = -1;
    opts.duration_ms = -1;
    opts.samples = -1;
    const wchar_t *command = NULL;

    for (int i = 1; i < argc; ++i) {
        const wchar_t *t = argv[i];

        if (wcscmp(t, L"--help") == 0)              opts.help = 1;
        else if (wcscmp(t, L"--version") == 0)      opts.version = 1;
        else if (wcscmp(t, L"--verbose") == 0)      opts.verbose = 1;
        else if (wcscmp(t, L"--debug") == 0)        opts.debug = 1;
        else if (wcscmp(t, L"--json") == 0)         opts.json = 1;
        else if (wcscmp(t, L"--no-color") == 0)     opts.no_color = 1;
        else if (wcscmp(t, L"--no-unicode") == 0)   opts.no_unicode = 1;
        else if (wcscmp(t, L"--safe-terminal") == 0) opts.safe_terminal = 1;
        else if (wcscmp(t, L"--yes") == 0)          opts.yes = 1;
        else if (wcscmp(t, L"--watch") == 0)        opts.watch = 1;
        else if (wcscmp(t, L"--no-recommendations") == 0) opts.no_recommendations = 1;
        else if (wcscmp(t, L"--include-services") == 0) opts.include_services = 1;
        else if (wcscmp(t, L"--include-tasks") == 0) opts.include_tasks = 1;
        else if (wcscmp(t, L"--measured") == 0)    opts.measured = 1;
        else if (wcscmp(t, L"--auto") == 0)         opts.svc_auto = 1;
        else if (wcscmp(t, L"--running") == 0)      opts.svc_running = 1;
        else if (wcscmp(t, L"--stopped") == 0)      opts.svc_stopped = 1;
        else if (wcscmp(t, L"--failed") == 0)       opts.svc_failed = 1;
        else if (wcscmp(t, L"--output") == 0) {
            if (i + 1 < argc) {
                opts.output_path = argv[++i];
            } else {
                fprintf(stderr, "wintune: --output requires a path argument\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (wcscmp(t, L"--limit") == 0) {
            if (i + 1 < argc) opts.limit = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --limit requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--interval") == 0) {
            if (i + 1 < argc) opts.interval_ms = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --interval requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--duration") == 0) {
            if (i + 1 < argc) opts.duration_ms = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --duration requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--samples") == 0) {
            if (i + 1 < argc) opts.samples = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --samples requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--sort") == 0) {
            if (i + 1 < argc) opts.sort = argv[++i];
            else { fprintf(stderr, "wintune: --sort requires a key\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--set") == 0) {
            if (i + 1 < argc) opts.set_value = argv[++i];
            else { fprintf(stderr, "wintune: --set requires a plan name\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--format") == 0) {
            if (i + 1 < argc) opts.format = argv[++i];
            else { fprintf(stderr, "wintune: --format requires text or json\n"); return WT_EXIT_USAGE; }
        }
        else if (t[0] == L'-') {
            fwprintf(stderr, L"wintune: unknown option '%ls'\n", t);
            return WT_EXIT_USAGE;
        } else if (command == NULL) {
            command = t;
        } else if (opts.arg1 == NULL) {
            opts.arg1 = t;
        } else if (opts.arg2 == NULL) {
            opts.arg2 = t;
        }
        /* Extra positional args beyond two are ignored. */
    }

    wt_apply_log_level(&opts);
    wt_cli_apply_session_defaults(&opts);
    WT_LOGD("parsed command=%ls json=%d interactive=%d remote=%d",
            command ? command : L"(none)", opts.json,
            wt_session_is_interactive(), wt_session_is_remote());

    /* Enable VT only for local/interactive color sessions. */
    if (!opts.no_color && wt_console_is_interactive()) {
        (void)wt_console_enable_vt();
    }

    /* Global flags take precedence over a command name. */
    if (opts.help || (command && wcscmp(command, L"help") == 0)) {
        wt_print_usage();
        return WT_EXIT_OK;
    }
    if (opts.version || (command && wcscmp(command, L"version") == 0)) {
        wt_print_version();
        return WT_EXIT_OK;
    }

    if (command == NULL) {
        wt_print_usage();
        return WT_EXIT_OK;
    }

    int rc;
    if (wcscmp(command, L"scan") == 0) {
        rc = wt_cmd_scan(&opts);
    } else if (wcscmp(command, L"top") == 0) {
        rc = wt_cmd_top(&opts);
    } else if (wcscmp(command, L"recommend") == 0) {
        rc = wt_cmd_recommend(&opts);
    } else if (wcscmp(command, L"doctor") == 0) {
        rc = wt_cmd_doctor(&opts);
    } else if (wcscmp(command, L"startup") == 0) {
        rc = wt_cmd_startup(&opts);
    } else if (wcscmp(command, L"services") == 0) {
        rc = wt_cmd_services(&opts);
    } else if (wcscmp(command, L"power") == 0) {
        rc = wt_cmd_power(&opts);
    } else if (wcscmp(command, L"apply") == 0) {
        rc = wt_cmd_apply(&opts);
    } else if (wcscmp(command, L"rollback") == 0) {
        rc = wt_cmd_rollback(&opts);
    } else if (wcscmp(command, L"report") == 0) {
        rc = wt_cmd_report(&opts);
    } else if (wcscmp(command, L"boot") == 0) {
        rc = wt_cmd_boot(&opts);
    } else if (wcscmp(command, L"tui") == 0) {
        rc = (wt_tui_run(&opts) == WT_OK) ? WT_EXIT_OK : WT_EXIT_NOT_IMPLEMENTED;
    } else if (wt_command_is_known(command)) {
        fwprintf(stderr,
                 L"wintune: '%ls' is recognized but not implemented yet "
                 L"(planned for a later phase).\n"
                 L"Run 'wintune help' to see available commands.\n",
                 command);
        rc = WT_EXIT_NOT_IMPLEMENTED;
    } else {
        fwprintf(stderr, L"wintune: unknown command '%ls'\n", command);
        fprintf(stderr, "Run 'wintune help' to see available commands.\n");
        rc = WT_EXIT_USAGE;
    }

    /* Some Windows providers used during collection (notably PDH) can leave the
     * process exit path from flushing block-buffered stdio (this only shows up
     * when output is redirected to a file or pipe, e.g. over SSH). Flush
     * explicitly so output is never silently dropped. */
    fflush(stdout);
    fflush(stderr);
    return rc;
}
