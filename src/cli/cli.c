#include "cli/cli.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "cli/cli_launcher.h"
#include "cli/commands_scan.h"
#include "cli/commands_top.h"
#include "cli/commands_recommend.h"
#include "cli/commands_doctor.h"
#include "cli/commands_startup.h"
#include "cli/commands_tasks.h"
#include "cli/commands_updates.h"
#include "cli/commands_blockers.h"
#include "cli/commands_services.h"
#include "cli/commands_power.h"
#include "cli/commands_apply.h"
#include "cli/commands_rollback.h"
#include "cli/commands_report.h"
#include "cli/commands_boot.h"
#include "cli/commands_service.h"
#include "cli/commands_tray.h"
#include "tui/tui.h"
#include "common/error.h"
#include "common/log.h"
#include "platform/console.h"
#include "platform/service_client.h"
#include "output/json.h"
#include "system/privilege.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>

/* Exit codes: see cli/exit_codes.h (WT_EXIT_*). */

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
        "  tasks       Scheduled tasks (logon/boot startup impact)\n"
        "  updates     Windows Update and reboot readiness\n"
        "  blockers    Apps/files blocking restart or updates\n"
        "  boot        Boot/login performance analysis (ETW-backed)\n"
        "  service     Install/manage the WinTune background agent\n"
        "  services    Show service status and startup type\n"
        "  power       Show current power plan and recommendations\n"
        "  recommend   Generate recommendations without applying them\n"
        "  apply       Apply a specific recommendation\n"
        "  report      Write a local performance report\n"
        "  doctor      scan + recommend + summary (best for normal users)\n"
        "  tray        System tray icon (native Win32, read-only by default)\n"
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
        "  --yes             Confirm mutating actions (dangerous actions stay blocked)\n"
        "  --via-service     Use the local WinTune service for privileged work\n"
        "  --json-errors     Emit machine-readable JSON on failure\n"
        "  --compact-json    Minified JSON (no pretty-printing)\n"
        "  --ndjson          One JSON document per line (e.g. top --watch --json)\n"
        "  --log-file <path> Append verbose/debug logs to a file (also stderr)\n"
        "  --theme <name>    TUI theme: default | compact | mono\n",
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
        L"scan", L"top", L"tui", L"startup", L"tasks", L"updates", L"blockers",
        L"boot", L"service", L"services", L"power",
        L"recommend", L"apply", L"report", L"doctor", L"tray", L"rollback"
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

int wt_cli_should_route_via_service(const WT_CliOptions *opts)
{
    if (opts != NULL && opts->via_service) {
        return 1;
    }
    if (!wt_is_process_elevated() && wt_service_client_is_available(500)) {
        return 1;
    }
    return 0;
}

void wt_cli_configure_json_output(const WT_CliOptions *opts)
{
    wt_json_apply_cli_options(opts);
}

int wt_cli_run(int argc, wchar_t **argv)
{
    WT_CliOptions opts;
    const wchar_t *command = NULL;
    int parse_rc = wt_cli_parse_argv(argc, argv, &opts, &command);
    if (parse_rc != WT_EXIT_OK) {
        return parse_rc;
    }

    wt_apply_log_level(&opts);
    if (opts.log_file_path != NULL) {
        WT_Result lr = wt_log_open_file(opts.log_file_path);
        if (lr != WT_OK) {
            fwprintf(stderr,
                     L"wintune: could not open log file '%ls' (%hs)\n",
                     opts.log_file_path, wt_result_to_string(lr));
            return WT_EXIT_ERROR;
        }
    }
    wt_cli_apply_session_defaults(&opts);
    if (opts.compact_json && opts.ndjson) {
        /* --ndjson implies compact single-line documents. */
        opts.compact_json = 1;
    }
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
        if (wt_cli_should_show_launcher(argc)) {
            return wt_cli_interactive_launcher();
        }
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
    } else if (wcscmp(command, L"tasks") == 0) {
        rc = wt_cmd_tasks(&opts);
    } else if (wcscmp(command, L"updates") == 0) {
        rc = wt_cmd_updates(&opts);
    } else if (wcscmp(command, L"blockers") == 0) {
        rc = wt_cmd_blockers(&opts);
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
    } else if (wcscmp(command, L"service") == 0) {
        rc = wt_cmd_service(&opts);
    } else if (wcscmp(command, L"tray") == 0) {
        rc = wt_cmd_tray(&opts);
    } else if (wcscmp(command, L"tui") == 0) {
        rc = (wt_tui_run(&opts) == WT_OK) ? WT_EXIT_OK : WT_EXIT_ERROR;
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
    return wt_cli_finish(&opts, rc, command);
}
