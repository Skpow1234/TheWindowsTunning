#include "cli/commands_fleet.h"

#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "fleet/fleet_pack.h"
#include "output/json.h"
#include "platform/time.h"
#include "common/sha256.h"
#include "common/error.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
    int json_mode;

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

    json_mode = wt_cli_is_json_mode(opts);

    if (json_mode) {
        /* Pack silently; emit result JSON after success. */
        r = wt_fleet_pack(input, output, NULL);
    } else {
        r = wt_fleet_pack(input, output, stdout);
    }

    if (r != WT_OK) {
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

    if (json_mode) {
        unsigned char dig[32];
        char hex[65];
        size_t count = 0;
        /* Re-count reports for JSON (lightweight dir walk). */
        {
            wchar_t pattern[MAX_PATH];
            WIN32_FIND_DATAW fd;
            HANDLE find;
            _snwprintf_s(pattern, MAX_PATH, _TRUNCATE, L"%ls\\*.json", input);
            find = FindFirstFileW(pattern, &fd);
            if (find != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                        _wcsicmp(fd.cFileName, L"manifest.json") != 0) {
                        count++;
                    }
                } while (FindNextFileW(find, &fd));
                FindClose(find);
            }
        }
        hex[0] = '\0';
        if (wt_sha256_file(output, dig) == WT_OK) {
            wt_sha256_hex(dig, hex);
        }
        return wt_fleet_emit_json(opts, input, output, count, hex);
    }

    return WT_EXIT_OK;
}
