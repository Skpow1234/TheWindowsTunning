#include "cli/commands_boot.h"
#include "cli/cli_exit.h"
#include "cli/exit_codes.h"
#include "system/boot.h"
#include "system/privilege.h"
#include "output/json.h"
#include "common/error.h"
#include "common/units.h"
#include "platform/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <strsafe.h>

static FILE *wt_open_output(const WT_CliOptions *opts, FILE **opened)
{
    *opened = NULL;
    if (opts != NULL && opts->output_path != NULL) {
        FILE *f = NULL;
        if (_wfopen_s(&f, opts->output_path, L"wb") != 0 || f == NULL) {
            fwprintf(stderr, L"wintune: could not open output file '%ls'\n",
                     opts->output_path);
            return NULL;
        }
        *opened = f;
        return f;
    }
    return stdout;
}

static void wt_print_boot_text(const WT_BootReport *boot)
{
    printf("WinTune Boot Analysis\n\n");

    if (boot->boot_duration_ms > 0) {
        wchar_t dur[32];
        wt_format_duration_ms(boot->boot_duration_ms, dur, ARRAYSIZE(dur));
        printf("Last boot duration: %ls\n", dur);
    } else {
        printf("Last boot duration: (unavailable)\n");
    }

    if (boot->main_path_ms > 0 || boot->post_boot_ms > 0) {
        printf("\nBoot breakdown:\n");
        if (boot->main_path_ms > 0) {
            wchar_t t[32];
            wt_format_duration_ms(boot->main_path_ms, t, ARRAYSIZE(t));
            printf("  Main path:     %ls\n", t);
        }
        if (boot->kernel_init_ms > 0) {
            wchar_t t[32];
            wt_format_duration_ms(boot->kernel_init_ms, t, ARRAYSIZE(t));
            printf("  Kernel init:   %ls\n", t);
        }
        if (boot->driver_init_ms > 0) {
            wchar_t t[32];
            wt_format_duration_ms(boot->driver_init_ms, t, ARRAYSIZE(t));
            printf("  Driver init:   %ls\n", t);
        }
        if (boot->post_boot_ms > 0) {
            wchar_t t[32];
            wt_format_duration_ms(boot->post_boot_ms, t, ARRAYSIZE(t));
            printf("  Post-boot:     %ls\n", t);
        }
    }

    if (boot->is_degraded) {
        printf("\nBoot degradation: yes\n");
        if (boot->degradation_summary[0] != L'\0') {
            wprintf(L"  Summary: %ls\n", boot->degradation_summary);
        }
    }

    if (boot->component_count > 0) {
        printf("\nSlow startup components (%zu):\n\n", boot->component_count);
        printf("%-12s %-8s %ls\n", "Kind", "Time", L"Name");
        for (size_t i = 0; i < boot->component_count; ++i) {
            const WT_BootComponent *c = &boot->components[i];
            wchar_t t[32];
            wt_format_duration_ms(c->duration_ms, t, ARRAYSIZE(t));
            wprintf(L"%-12hs %-8ls %ls\n",
                    wt_boot_component_kind_name(c->kind),
                    t,
                    c->name);
            if (c->detail[0] != L'\0') {
                wprintf(L"             %ls\n", c->detail);
            }
            if (c->is_disk_heavy) {
                printf("             (disk I/O during startup)\n");
            }
        }
    } else {
        printf("\nNo per-component boot degradation events were recorded.\n");
    }

    if (boot->trace_path[0] != L'\0') {
        wprintf(L"\nTrace file: %ls\n", boot->trace_path);
        if (boot->etl_event_count > 0) {
            printf("Trace events: %lu\n", boot->etl_event_count);
        }
    }

    wprintf(L"\nSource: %ls (Windows Diagnostic-Performance event log)\n",
            boot->source);
    printf("Run 'wintune startup --measured' to correlate startup entries.\n");
}

static int wt_boot_cmd_trace(const WT_CliOptions *opts)
{
    unsigned duration = 60000;
    if (opts != NULL && opts->duration_ms > 0) {
        duration = (unsigned)opts->duration_ms;
    } else if (opts != NULL && opts->interval_ms > 0) {
        duration = (unsigned)opts->interval_ms;
    }

    if (!opts->json) {
        printf("Starting login ETW trace for %u ms ...\n", duration);
        printf("(This traces the current session, not the next reboot.)\n");
    }

    wchar_t etl_path[MAX_PATH];
    WT_Result r = wt_boot_trace_login(duration, etl_path, ARRAYSIZE(etl_path));
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return 1;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: boot trace failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    if (opts != NULL && opts->json) {
        WT_BootReport boot;
        wt_boot_report_init(&boot);
        StringCchCopyW(boot.trace_path, ARRAYSIZE(boot.trace_path), etl_path);
        (void)wt_boot_analyze_etl(etl_path, &boot);

        FILE *opened = NULL;
        FILE *out = wt_open_output(opts, &opened);
        if (out == NULL) {
            return 1;
        }
        wt_print_boot_json(&boot, out);
        if (opened != NULL) {
            fclose(opened);
        }
    } else {
        wprintf(L"Trace saved: %ls\n", etl_path);
        printf("Analyze with: wintune boot analyze \"%ls\"\n", etl_path);
    }

    return 0;
}

static int wt_boot_cmd_analyze(const WT_CliOptions *opts)
{
    const wchar_t *etl = NULL;
    if (opts != NULL && opts->arg2 != NULL) {
        etl = opts->arg2;
    }

    WT_BootReport boot;
    WT_Result r = wt_collect_boot_report(&boot, etl);
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return wt_cli_exit_from_result(opts, r, L"boot",
                                       "boot analyze requires administrator privileges");
    }
    if (r == WT_ERR_NOT_FOUND) {
        fprintf(stderr,
                "wintune: no boot performance data found.\n"
                "Ensure the Diagnostic-Performance event log is enabled.\n"
                "You may need to reboot once so Windows records boot metrics.\n");
        return wt_cli_exit_from_result(
            opts, r, L"boot",
            "Diagnostic-Performance channel or boot events not found");
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: boot analyze failed (%s)\n",
                wt_result_to_string(r));
        return wt_cli_exit_from_result(opts, r, L"boot", NULL);
    }

    if (opts != NULL && opts->json) {
        FILE *opened = NULL;
        FILE *out = wt_open_output(opts, &opened);
        if (out == NULL) {
            return 1;
        }
        wt_print_boot_json(&boot, out);
        if (opened != NULL) {
            fclose(opened);
        }
    } else {
        wt_print_boot_text(&boot);
    }

    return 0;
}

int wt_cmd_boot(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "Usage:\n"
                "  wintune boot analyze [trace.etl]\n"
                "  wintune boot trace [--duration MS]\n");
        return 2;
    }

    if (wcscmp(opts->arg1, L"trace") == 0) {
        return wt_boot_cmd_trace(opts);
    }
    if (wcscmp(opts->arg1, L"analyze") == 0) {
        return wt_boot_cmd_analyze(opts);
    }

    fwprintf(stderr,
             L"wintune: unknown boot subcommand '%ls'. "
             L"Use 'analyze' or 'trace'.\n",
             opts->arg1);
    return 2;
}
