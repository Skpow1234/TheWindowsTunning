#include "cli/cli.h"
#include "cli/exit_codes.h"
#include "system/power.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_int(int got, int want, const char *label)
{
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", label, got, want);
        g_failed = 1;
    }
}

static void expect_wstr(const wchar_t *got, const wchar_t *want, const char *label)
{
    if ((got == NULL) != (want == NULL) ||
        (got != NULL && want != NULL && wcscmp(got, want) != 0)) {
        fwprintf(stderr, L"FAIL %hs: got '%ls' want '%ls'\n", label,
                 got ? got : L"(null)", want ? want : L"(null)");
        g_failed = 1;
    }
}

int main(void)
{
    WT_CliOptions opts;
    const wchar_t *cmd = NULL;

    {
        wchar_t *argv[] = {
            L"wintune", L"scan", L"--json", L"--samples", L"3",
            L"--interval", L"500", L"--theme", L"compact"
        };
        expect_int(wt_cli_parse_argv(9, argv, &opts, &cmd), WT_EXIT_OK, "parse ok");
        expect_wstr(cmd, L"scan", "cmd scan");
        expect_int(opts.json, 1, "json");
        expect_int((int)opts.samples, 3, "samples");
        expect_int((int)opts.interval_ms, 500, "interval");
        expect_wstr(opts.theme, L"compact", "theme");
    }

    {
        wchar_t *argv[] = { L"wintune", L"apply", L"WT-POWER-001", L"--yes" };
        expect_int(wt_cli_parse_argv(4, argv, &opts, &cmd), WT_EXIT_OK, "apply");
        expect_wstr(cmd, L"apply", "cmd apply");
        expect_wstr(opts.arg1, L"WT-POWER-001", "arg1");
        expect_int(opts.yes, 1, "yes");
    }

    {
        wchar_t *argv[] = {
            L"wintune", L"apply", L"WT-POWER-001", L"--dry-run", L"--json"
        };
        expect_int(wt_cli_parse_argv(5, argv, &opts, &cmd), WT_EXIT_OK,
                   "dry-run");
        expect_int(opts.dry_run, 1, "dry_run");
        expect_int(opts.json, 1, "dry_run json");
    }

    {
        wchar_t *argv[] = { L"wintune", L"doctor", L"--plan", L"--json" };
        expect_int(wt_cli_parse_argv(4, argv, &opts, &cmd), WT_EXIT_OK,
                   "doctor plan");
        expect_wstr(cmd, L"doctor", "cmd doctor");
        expect_int(opts.plan, 1, "plan");
        expect_int(opts.json, 1, "plan json");
    }

    {
        wchar_t *argv[] = { L"wintune", L"scan", L"--limit" };
        expect_int(wt_cli_parse_argv(3, argv, &opts, &cmd), WT_EXIT_USAGE,
                   "limit missing");
    }

    {
        wchar_t *argv[] = { L"wintune", L"scan", L"--not-a-real-flag" };
        expect_int(wt_cli_parse_argv(3, argv, &opts, &cmd), WT_EXIT_USAGE,
                   "unknown flag");
    }

    {
        wchar_t *argv[] = { L"wintune", L"report", L"--format", L"json" };
        expect_int(wt_cli_parse_argv(4, argv, &opts, &cmd), WT_EXIT_OK, "format");
        opts.json = 0;
        expect_int(wt_cli_is_json_mode(&opts), 1, "report format json mode");
    }

    {
        wchar_t *argv[] = {
            L"wintune", L"fleet", L"pack", L"--input", L"reports",
            L"--output", L"fleet-pack.zip"
        };
        expect_int(wt_cli_parse_argv(7, argv, &opts, &cmd), WT_EXIT_OK,
                   "fleet pack");
        expect_wstr(cmd, L"fleet", "cmd fleet");
        expect_wstr(opts.arg1, L"pack", "fleet arg1 pack");
        expect_wstr(opts.input_path, L"reports", "input");
        expect_wstr(opts.output_path, L"fleet-pack.zip", "output");
    }

    {
        wchar_t *argv[] = {
            L"wintune", L"scan", L"--json", L"--schema-version", L"1"
        };
        expect_int(wt_cli_parse_argv(5, argv, &opts, &cmd), WT_EXIT_OK,
                   "schema-version");
        expect_wstr(opts.schema_version, L"1", "schema pin 1");
    }

    expect_int(wt_power_scheme_from_token(L"performance"), WT_POWER_HIGH_PERF,
               "power token performance");
    expect_int(wt_power_scheme_from_token(L"balanced"), WT_POWER_BALANCED,
               "power token balanced");
    expect_int(wt_power_scheme_from_token(L"nope"), WT_POWER_UNKNOWN,
               "power token unknown");

    if (g_failed) {
        fputs("cli parser tests failed\n", stderr);
        return 1;
    }
    fputs("cli parser tests passed\n", stdout);
    return 0;
}
