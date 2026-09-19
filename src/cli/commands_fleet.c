#include "cli/commands_fleet.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "common/error.h"
#include "fleet/fleet_pack.h"
#include "output/json.h"
#include "platform/time.h"

#include <stdio.h>
#include <wchar.h>

static void wt_fleet_print_usage(void)
{
    fprintf(stderr,
            "Usage:\n"
            "  wintune fleet pack --input <dir> --output <file.zip>\n"
            "\n"
            "Bundles *.json scan reports from <dir> into a local ZIP with\n"
            "manifest.json and CHECKSUMS.sha256. Never uploads.\n"
            "\n"
            "Defaults:\n"
            "  --output fleet-pack.zip\n");
}

static int wt_fleet_emit_json(const WT_CliOptions *opts, const wchar_t *input,
                              const wchar_t *output, size_t report_count,
                              const char *zip_sha256)
{
    WT_JsonWriter w;
    char created[32];

    wt_cli_configure_json_output(opts);
    wt_json_init(&w, stdout);
    wt_json_begin_object(&w);
    wt_json_key(&w, "schema_version");
    wt_json_string(&w, WT_JSON_SCHEMA_VERSION);
    wt_json_key(&w, "kind");
    wt_json_string(&w, "fleet_pack_result");
    wt_json_key(&w, "created_utc");
    if (wt_now_iso8601_utc(created, sizeof(created)) == WT_OK) {
        wt_json_string(&w, created);
    } else {
        wt_json_string(&w, "");
    }
    wt_json_key(&w, "input");
    wt_json_wstring(&w, input);
    wt_json_key(&w, "output");
    wt_json_wstring(&w, output);
    wt_json_key(&w, "report_count");
    wt_json_uint64(&w, (unsigned long long)report_count);
    wt_json_key(&w, "local_only");
    wt_json_bool(&w, 1);
    wt_json_key(&w, "upload");
    wt_json_bool(&w, 0);
    if (zip_sha256 != NULL && zip_sha256[0] != '\0') {
        wt_json_key(&w, "zip_sha256");
        wt_json_string(&w, zip_sha256);
    }
    wt_json_end_object(&w);
    wt_json_finish(&w);
    return WT_EXIT_OK;
}

int wt_cmd_fleet(const WT_CliOptions *opts)
{
    const wchar_t *sub;
    const wchar_t *input;
    const wchar_t *output;
    WT_Result r;
    size_t report_count = 0;
    char zip_hex[65];

    if (opts == NULL) {
        return WT_EXIT_USAGE;
    }

    sub = opts->arg1;
    if (sub == NULL) {
        wt_fleet_print_usage();
        return wt_cli_exit_usage(opts, L"fleet", "missing fleet subcommand");
    }

    if (wcscmp(sub, L"pack") != 0) {
        fwprintf(stderr,
                 L"wintune: unknown fleet subcommand '%ls'. Use 'pack'.\n",
                 sub);
        wt_fleet_print_usage();
        return wt_cli_exit_usage(opts, L"fleet", "unknown fleet subcommand");
    }

    input = opts->input_path;
    if (input == NULL || input[0] == L'\0') {
        fprintf(stderr, "wintune: fleet pack requires --input <dir>\n");
        wt_fleet_print_usage();
        return wt_cli_exit_usage(opts, L"fleet", "fleet pack requires --input");
    }

    output = opts->output_path;
    if (output == NULL || output[0] == L'\0') {
        output = L"fleet-pack.zip";
    }

    zip_hex[0] = '\0';
    if (wt_cli_is_json_mode(opts)) {
        r = wt_fleet_pack(input, output, NULL, &report_count, zip_hex);
        if (r != WT_OK) {
            goto fail;
        }
        return wt_fleet_emit_json(opts, input, output, report_count, zip_hex);
    }

    r = wt_fleet_pack(input, output, stdout, &report_count, zip_hex);
    if (r != WT_OK) {
        goto fail;
    }
    return WT_EXIT_OK;

fail:
    {
        const char *msg = wt_result_to_string(r);
        if (r == WT_ERR_NOT_FOUND) {
            msg = "No JSON report files found in --input (or path missing).";
        } else if (r == WT_ERR_INVALID_ARGUMENT) {
            msg = "Invalid --input or --output path.";
        } else if (r == WT_ERR_NOT_SUPPORTED) {
            msg = "Too many files or a report exceeds the pack size limit.";
        }
        return wt_cli_exit_from_result(opts, r, L"fleet", msg);
    }
}
