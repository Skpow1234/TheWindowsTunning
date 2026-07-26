#include "cli/cli.h"
#include "cli/exit_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

int wt_cli_parse_argv(int argc, wchar_t **argv, WT_CliOptions *opts,
                      const wchar_t **out_command)
{
    if (opts == NULL) {
        return WT_EXIT_ERROR;
    }
    memset(opts, 0, sizeof(*opts));
    opts->limit = -1;
    opts->interval_ms = -1;
    opts->duration_ms = -1;
    opts->samples = -1;
    opts->delay_seconds = -1;
    if (out_command != NULL) {
        *out_command = NULL;
    }

    const wchar_t *command = NULL;

    for (int i = 1; i < argc; ++i) {
        const wchar_t *t = argv[i];

        if (wcscmp(t, L"--help") == 0)              opts->help = 1;
        else if (wcscmp(t, L"--version") == 0)      opts->version = 1;
        else if (wcscmp(t, L"--verbose") == 0)      opts->verbose = 1;
        else if (wcscmp(t, L"--debug") == 0)        opts->debug = 1;
        else if (wcscmp(t, L"--json") == 0)         opts->json = 1;
        else if (wcscmp(t, L"--no-color") == 0)     opts->no_color = 1;
        else if (wcscmp(t, L"--no-unicode") == 0)   opts->no_unicode = 1;
        else if (wcscmp(t, L"--safe-terminal") == 0) opts->safe_terminal = 1;
        else if (wcscmp(t, L"--yes") == 0)          opts->yes = 1;
        else if (wcscmp(t, L"--watch") == 0)        opts->watch = 1;
        else if (wcscmp(t, L"--no-recommendations") == 0) opts->no_recommendations = 1;
        else if (wcscmp(t, L"--include-services") == 0) opts->include_services = 1;
        else if (wcscmp(t, L"--include-tasks") == 0) opts->include_tasks = 1;
        else if (wcscmp(t, L"--logon") == 0)        opts->tasks_logon = 1;
        else if (wcscmp(t, L"--seconds") == 0) {
            if (i + 1 < argc) {
                opts->delay_seconds = wcstol(argv[++i], NULL, 10);
            } else {
                fprintf(stderr, "wintune: --seconds requires a number\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (wcscmp(t, L"--measured") == 0)    opts->measured = 1;
        else if (wcscmp(t, L"--via-service") == 0) opts->via_service = 1;
        else if (wcscmp(t, L"--json-errors") == 0) opts->json_errors = 1;
        else if (wcscmp(t, L"--compact-json") == 0) opts->compact_json = 1;
        else if (wcscmp(t, L"--ndjson") == 0) opts->ndjson = 1;
        else if (wcscmp(t, L"--log-file") == 0) {
            if (i + 1 < argc) {
                opts->log_file_path = argv[++i];
            } else {
                fprintf(stderr, "wintune: --log-file requires a path\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (wcscmp(t, L"--auto-start") == 0) opts->service_auto_start = 1;
        else if (wcscmp(t, L"--account") == 0) {
            if (i + 1 < argc) {
                opts->service_account = argv[++i];
            } else {
                fprintf(stderr, "wintune: --account requires a value\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (wcscmp(t, L"--account-password") == 0) {
            if (i + 1 < argc) {
                opts->service_account_password = argv[++i];
            } else {
                fprintf(stderr,
                        "wintune: --account-password requires a value\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (wcscmp(t, L"--auto") == 0)         opts->svc_auto = 1;
        else if (wcscmp(t, L"--running") == 0)      opts->svc_running = 1;
        else if (wcscmp(t, L"--stopped") == 0)      opts->svc_stopped = 1;
        else if (wcscmp(t, L"--failed") == 0)       opts->svc_failed = 1;
        else if (wcscmp(t, L"--output") == 0) {
            if (i + 1 < argc) {
                opts->output_path = argv[++i];
            } else {
                fprintf(stderr, "wintune: --output requires a path argument\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (wcscmp(t, L"--limit") == 0) {
            if (i + 1 < argc) opts->limit = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --limit requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--interval") == 0) {
            if (i + 1 < argc) opts->interval_ms = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --interval requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--duration") == 0) {
            if (i + 1 < argc) opts->duration_ms = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --duration requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--samples") == 0) {
            if (i + 1 < argc) opts->samples = wcstol(argv[++i], NULL, 10);
            else { fprintf(stderr, "wintune: --samples requires a number\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--sort") == 0) {
            if (i + 1 < argc) opts->sort = argv[++i];
            else { fprintf(stderr, "wintune: --sort requires a key\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--set") == 0) {
            if (i + 1 < argc) opts->set_value = argv[++i];
            else { fprintf(stderr, "wintune: --set requires a plan name\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--format") == 0) {
            if (i + 1 < argc) opts->format = argv[++i];
            else { fprintf(stderr, "wintune: --format requires text or json\n"); return WT_EXIT_USAGE; }
        }
        else if (wcscmp(t, L"--theme") == 0) {
            if (i + 1 < argc) opts->theme = argv[++i];
            else {
                fprintf(stderr,
                        "wintune: --theme requires a name (default|compact|mono)\n");
                return WT_EXIT_USAGE;
            }
        }
        else if (t[0] == L'-') {
            fwprintf(stderr, L"wintune: unknown option '%ls'\n", t);
            return WT_EXIT_USAGE;
        } else if (command == NULL) {
            command = t;
        } else if (opts->arg1 == NULL) {
            opts->arg1 = t;
        } else if (opts->arg2 == NULL) {
            opts->arg2 = t;
        }
        /* Extra positional args beyond two are ignored. */
    }

    if (out_command != NULL) {
        *out_command = command;
    }
    return WT_EXIT_OK;
}

