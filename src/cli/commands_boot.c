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
        printf("Last boot duration: %ls", dur);
        if (boot->last_boot_kind != WT_BOOT_KIND_UNKNOWN) {
            printf(" (%s)", wt_boot_kind_name(boot->last_boot_kind));
        }
        printf("\n");
    } else {
        printf("Last boot duration: (unavailable)\n");
    }

    if (boot->history.count > 0) {
        printf("\nBoot history (%zu recent):\n", boot->history.count);
        printf("  Average: %.1f s  |  Slow (>=60s): %u  |  Degraded: %u\n",
               boot->history.avg_duration_ms / 1000.0,
               boot->history.slow_count, boot->history.degraded_count);
        printf("  Cold: %u", boot->history.cold_count);
        if (boot->history.cold_count > 0) {
            printf(" (avg %.1f s)", boot->history.avg_cold_ms / 1000.0);
        }
        printf("  |  Warm/hybrid: %u", boot->history.warm_count);
        if (boot->history.warm_count > 0) {
            printf(" (avg %.1f s)", boot->history.avg_warm_ms / 1000.0);
        }
        if (boot->history.unknown_count > 0) {
            printf("  |  Unknown: %u", boot->history.unknown_count);
        }
        printf("\n");
        for (size_t i = 0; i < boot->history.count && i < 5; ++i) {
            const WT_BootHistoryEntry *e = &boot->history.entries[i];
            wchar_t t[32];
            wt_format_duration_ms(e->boot_duration_ms, t, ARRAYSIZE(t));
            printf("  [%zu] %-5s %ls", i + 1, wt_boot_kind_name(e->kind), t);
            if (e->boot_start_utc[0] != '\0') {
                printf("  %s", e->boot_start_utc);
            }
            if (e->is_degraded) {
                printf("  (degraded)");
            }
            printf("\n");
        }
        if (boot->history.count > 5) {
            printf("  ... %zu more\n", boot->history.count - 5);
        }
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
        printf("\nDriver / service start waterfall (%zu, by impact)",
               boot->component_count);
        if (boot->waterfall_total_ms > 0) {
            wchar_t tot[32];
            wt_format_duration_ms(boot->waterfall_total_ms, tot, ARRAYSIZE(tot));
            wprintf(L", total attributed %ls", tot);
        }
        printf(":\n\n");
        printf("%-4s %-12s %-8s %-8s %ls\n", "#", "Kind", "Time", "Offset",
               L"Name");
        for (size_t i = 0; i < boot->component_count; ++i) {
            const WT_BootComponent *c = &boot->components[i];
            wchar_t t[32];
            wchar_t off[32];
            wt_format_duration_ms(c->duration_ms, t, ARRAYSIZE(t));
            if (c->start_offset_ms > 0) {
                wt_format_duration_ms(c->start_offset_ms, off, ARRAYSIZE(off));
            } else {
                StringCchCopyW(off, ARRAYSIZE(off), L"—");
            }
            wprintf(L"%-4zu %-12hs %-8ls %-8ls %ls", i + 1,
                    wt_boot_component_kind_name(c->kind), t, off, c->name);
            if (c->event_id > 0) {
                printf("  [E%u]", c->event_id);
            }
            printf("\n");
            if (c->service_matched) {
                wprintf(L"             service %ls (%hs", c->service_name,
                        c->service_state);
                if (c->service_pid > 0) {
                    printf(", pid %lu", c->service_pid);
                }
                printf(")\n");
            }
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
        printf("For next-boot capture: wintune boot arm\n");
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
                "You may need to reboot once so Windows records boot metrics,\n"
                "or arm a next-boot trace with: wintune boot arm\n");
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

static int wt_boot_cmd_arm(const WT_CliOptions *opts)
{
    WT_Result r = wt_boot_arm_next();
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return wt_cli_exit_from_result(opts, r, L"boot",
                                       "boot arm requires administrator privileges");
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: boot arm failed (%s)\n",
                wt_result_to_string(r));
        return wt_cli_exit_from_result(opts, r, L"boot", NULL);
    }

    WT_BootArmStatus st;
    (void)wt_boot_arm_status(&st);

    if (opts != NULL && opts->json) {
        FILE *opened = NULL;
        FILE *out = wt_open_output(opts, &opened);
        if (out == NULL) {
            return 1;
        }
        fprintf(out,
                "{\"command\":\"boot arm\",\"state\":\"%s\",\"session\":\"",
                wt_boot_arm_state_name(st.state));
        /* Minimal JSON; path as escaped-ish wide via UTF-8 approx. */
        {
            char session_u8[128];
            WideCharToMultiByte(CP_UTF8, 0, st.session_name, -1, session_u8,
                                (int)sizeof(session_u8), NULL, NULL);
            fprintf(out, "%s\",\"etl_path\":\"", session_u8);
        }
        {
            char path_u8[MAX_PATH * 3];
            WideCharToMultiByte(CP_UTF8, 0, st.etl_path, -1, path_u8,
                                (int)sizeof(path_u8), NULL, NULL);
            for (const char *p = path_u8; *p; ++p) {
                if (*p == '\\' || *p == '"') {
                    fputc('\\', out);
                }
                fputc(*p, out);
            }
        }
        fprintf(out, "\",\"armed_utc\":\"%s\"}\n",
                st.armed_utc[0] != '\0' ? st.armed_utc : "");
        if (opened != NULL) {
            fclose(opened);
        }
        return 0;
    }

    printf("WinTune Boot Arm\n\n");
    wprintf(L"Autologger session '%ls' configured for the next reboot.\n",
            st.session_name);
    wprintf(L"Trace file (after reboot): %ls\n", st.etl_path);
    printf("\nNext steps:\n");
    printf("  1. Reboot this machine.\n");
    printf("  2. After login: wintune boot analyze\n");
    printf("  3. Optional: wintune boot disarm  (stops Autologger)\n");
    printf("\nRequires administrator privileges; does not change power or "
           "security settings.\n");
    return 0;
}

static int wt_boot_cmd_disarm(const WT_CliOptions *opts)
{
    int keep_etl = 1;
    WT_Result r = wt_boot_disarm(keep_etl);
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return wt_cli_exit_from_result(opts, r, L"boot",
                                       "boot disarm requires administrator privileges");
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: boot disarm failed (%s)\n",
                wt_result_to_string(r));
        return wt_cli_exit_from_result(opts, r, L"boot", NULL);
    }

    if (opts != NULL && opts->json) {
        FILE *opened = NULL;
        FILE *out = wt_open_output(opts, &opened);
        if (out == NULL) {
            return 1;
        }
        fprintf(out, "{\"command\":\"boot disarm\",\"state\":\"idle\","
                     "\"etl_kept\":true}\n");
        if (opened != NULL) {
            fclose(opened);
        }
        return 0;
    }

    printf("WinTune Boot Disarm\n\n");
    printf("Autologger session removed. Existing reboot ETL (if any) was kept "
           "under %%ProgramData%%\\WinTune\\traces\\.\n");
    return 0;
}

static int wt_boot_cmd_status(const WT_CliOptions *opts)
{
    WT_BootArmStatus st;
    WT_Result r = wt_boot_arm_status(&st);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: boot status failed (%s)\n",
                wt_result_to_string(r));
        return wt_cli_exit_from_result(opts, r, L"boot", NULL);
    }

    if (opts != NULL && opts->json) {
        FILE *opened = NULL;
        FILE *out = wt_open_output(opts, &opened);
        char path_u8[MAX_PATH * 3];
        char session_u8[128];
        if (out == NULL) {
            return 1;
        }
        WideCharToMultiByte(CP_UTF8, 0, st.session_name, -1, session_u8,
                            (int)sizeof(session_u8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, st.etl_path, -1, path_u8,
                            (int)sizeof(path_u8), NULL, NULL);
        fprintf(out,
                "{\"command\":\"boot status\",\"state\":\"%s\",\"session\":\"%s\","
                "\"reboot_occurred\":%s,\"etl_exists\":%s,\"etl_bytes\":%llu,"
                "\"armed_utc\":\"%s\",\"etl_path\":\"",
                wt_boot_arm_state_name(st.state), session_u8,
                st.reboot_occurred ? "true" : "false",
                st.etl_exists ? "true" : "false",
                (unsigned long long)st.etl_bytes,
                st.armed_utc[0] != '\0' ? st.armed_utc : "");
        for (const char *p = path_u8; *p; ++p) {
            if (*p == '\\' || *p == '"') {
                fputc('\\', out);
            }
            fputc(*p, out);
        }
        fprintf(out, "\"}\n");
        if (opened != NULL) {
            fclose(opened);
        }
        return 0;
    }

    printf("WinTune Boot Arm Status\n\n");
    printf("State:   %s\n", wt_boot_arm_state_name(st.state));
    wprintf(L"Session: %ls\n", st.session_name);
    if (st.armed_utc[0] != '\0') {
        printf("Armed:   %s\n", st.armed_utc);
    }
    printf("Reboot since arm: %s\n", st.reboot_occurred ? "yes" : "no");
    if (st.etl_path[0] != L'\0') {
        wprintf(L"ETL:     %ls\n", st.etl_path);
        if (st.etl_exists) {
            printf("         (%llu bytes)\n",
                   (unsigned long long)st.etl_bytes);
        } else {
            printf("         (not present yet)\n");
        }
    }
    if (st.state == WT_BOOT_ARM_PENDING_REBOOT) {
        printf("\nReboot to capture the next boot, then run: wintune boot analyze\n");
    } else if (st.state == WT_BOOT_ARM_READY ||
               st.state == WT_BOOT_ARM_CAPTURING) {
        printf("\nRun: wintune boot analyze\n");
        printf("Then: wintune boot disarm\n");
    }
    return 0;
}

int wt_cmd_boot(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "Usage:\n"
                "  wintune boot analyze [trace.etl]\n"
                "  wintune boot trace [--duration MS]\n"
                "  wintune boot arm\n"
                "  wintune boot disarm\n"
                "  wintune boot status\n");
        return 2;
    }

    if (wcscmp(opts->arg1, L"trace") == 0) {
        return wt_boot_cmd_trace(opts);
    }
    if (wcscmp(opts->arg1, L"analyze") == 0) {
        return wt_boot_cmd_analyze(opts);
    }
    if (wcscmp(opts->arg1, L"arm") == 0) {
        return wt_boot_cmd_arm(opts);
    }
    if (wcscmp(opts->arg1, L"disarm") == 0) {
        return wt_boot_cmd_disarm(opts);
    }
    if (wcscmp(opts->arg1, L"status") == 0) {
        return wt_boot_cmd_status(opts);
    }

    fwprintf(stderr,
             L"wintune: unknown boot subcommand '%ls'. "
             L"Use 'analyze', 'trace', 'arm', 'disarm', or 'status'.\n",
             opts->arg1);
    return 2;
}
