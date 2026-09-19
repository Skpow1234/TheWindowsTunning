#include "cli/commands_apps.h"
#include "system/uninstall.h"
#include "cli/cli.h"
#include "common/units.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wt_apps_print_guidance(FILE *out)
{
    fputs("\nWinTune never uninstalls software.\n", out);
    fputs("Official removal paths (you run these):\n", out);
    fputs("  Settings > Apps > Installed apps\n", out);
    fputs("  winget list\n", out);
    fputs("  winget uninstall \"<exact name from winget list>\"\n", out);
    fputs("  Control Panel > Programs and Features (Add/Remove Programs)\n", out);
    fputs("Do not delete Program Files folders by hand.\n", out);
}

static void wt_apps_print_text(const WT_UninstallAdvice *advice)
{
    printf("WinTune Uninstall Advisor (read-only)\n\n");
    printf("Scanned %zu uninstall registry keys; %zu visible apps; "
           "%zu review candidate(s).\n\n",
           advice->scanned_keys, advice->count, advice->candidate_count);

    if (advice->candidate_count == 0) {
        printf("No high-impact leftover candidates (startup correlation).\n");
        printf("Tip: wintune apps --large also lists large third-party installs.\n");
        printf("Use Settings / winget if you still want to remove unused apps.\n");
        wt_apps_print_guidance(stdout);
        return;
    }

    printf("Candidates for calm review:\n\n");
    printf("%-36s %-22s %8s  %s\n", "Name", "Publisher", "Size", "Why");
    printf("%-36s %-22s %8s  %s\n", "----", "---------", "----", "---");

    for (size_t i = 0; i < advice->count; ++i) {
        const WT_InstalledApp *a = &advice->apps[i];
        if (!a->candidate) {
            continue;
        }
        char size_buf[32] = "-";
        if (a->estimated_kb > 0) {
            wchar_t wsize[32];
            unsigned long long bytes =
                (unsigned long long)a->estimated_kb * 1024ull;
            if (wt_format_bytes(bytes, wsize, ARRAYSIZE(wsize)) == WT_OK) {
                WideCharToMultiByte(CP_UTF8, 0, wsize, -1, size_buf,
                                    sizeof(size_buf), NULL, NULL);
            }
        }
        char name_utf8[128];
        char pub_utf8[96];
        WideCharToMultiByte(CP_UTF8, 0, a->display_name, -1, name_utf8,
                            sizeof(name_utf8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, a->publisher, -1, pub_utf8,
                            sizeof(pub_utf8), NULL, NULL);
        if (name_utf8[36] != '\0') {
            name_utf8[33] = '.';
            name_utf8[34] = '.';
            name_utf8[35] = '.';
            name_utf8[36] = '\0';
        }
        if (pub_utf8[22] != '\0') {
            pub_utf8[19] = '.';
            pub_utf8[20] = '.';
            pub_utf8[21] = '.';
            pub_utf8[22] = '\0';
        }
        printf("%-36s %-22s %8s  %s%s\n", name_utf8, pub_utf8[0] ? pub_utf8 : "-",
               size_buf, a->reason,
               a->startup_related ? " [startup]" : "");
    }

    wt_apps_print_guidance(stdout);
}

static void wt_json_esc(FILE *out, const char *s)
{
    if (s == NULL) {
        return;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            fputc('\\', out);
            fputc((char)c, out);
        } else if (c < 0x20) {
            fprintf(out, "\\u%04x", c);
        } else {
            fputc((char)c, out);
        }
    }
}

static void wt_json_esc_w(FILE *out, const wchar_t *s)
{
    if (s == NULL) {
        return;
    }
    char utf8[1024];
    WideCharToMultiByte(CP_UTF8, 0, s, -1, utf8, sizeof(utf8), NULL, NULL);
    utf8[sizeof(utf8) - 1] = '\0';
    wt_json_esc(out, utf8);
}

static void wt_apps_print_json(const WT_UninstallAdvice *advice)
{
    fputs("{\n", stdout);
    fputs("  \"command\": \"apps\",\n", stdout);
    fputs("  \"read_only\": true,\n", stdout);
    fputs("  \"never_uninstalls\": true,\n", stdout);
    fprintf(stdout, "  \"scanned_keys\": %zu,\n", advice->scanned_keys);
    fprintf(stdout, "  \"app_count\": %zu,\n", advice->count);
    fprintf(stdout, "  \"candidate_count\": %zu,\n", advice->candidate_count);
    fputs("  \"guidance\": [\n", stdout);
    fputs("    \"Settings > Apps > Installed apps\",\n", stdout);
    fputs("    \"winget list\",\n", stdout);
    fputs("    \"winget uninstall \\\"<name>\\\" (user-run only)\",\n", stdout);
    fputs("    \"Control Panel > Programs and Features\"\n", stdout);
    fputs("  ],\n", stdout);
    fputs("  \"candidates\": [\n", stdout);

    int first = 1;
    for (size_t i = 0; i < advice->count; ++i) {
        const WT_InstalledApp *a = &advice->apps[i];
        if (!a->candidate) {
            continue;
        }
        if (!first) {
            fputs(",\n", stdout);
        }
        first = 0;
        fputs("    {\n", stdout);
        fputs("      \"display_name\": \"", stdout);
        wt_json_esc_w(stdout, a->display_name);
        fputs("\",\n", stdout);
        fputs("      \"publisher\": \"", stdout);
        wt_json_esc_w(stdout, a->publisher);
        fputs("\",\n", stdout);
        fputs("      \"version\": \"", stdout);
        wt_json_esc_w(stdout, a->version);
        fputs("\",\n", stdout);
        fputs("      \"install_location\": \"", stdout);
        wt_json_esc_w(stdout, a->install_location);
        fputs("\",\n", stdout);
        fprintf(stdout, "      \"estimated_kb\": %lu,\n", a->estimated_kb);
        fprintf(stdout, "      \"startup_related\": %s,\n",
                a->startup_related ? "true" : "false");
        fputs("      \"reason\": \"", stdout);
        wt_json_esc(stdout, a->reason);
        fputs("\",\n", stdout);
        fputs("      \"uninstall_string_present\": ", stdout);
        fputs(a->uninstall_string[0] != L'\0' ? "true" : "false", stdout);
        fputs("\n    }", stdout);
    }
    fputs("\n  ]\n", stdout);
    fputs("}\n", stdout);
}

int wt_cmd_apps(const WT_CliOptions *opts)
{
    WT_UninstallAdvice *advice =
        (WT_UninstallAdvice *)malloc(sizeof(WT_UninstallAdvice));
    if (advice == NULL) {
        fprintf(stderr, "wintune: out of memory\n");
        return 1;
    }
    WT_Result r = wt_collect_uninstall_advice(
        advice, 1, (opts != NULL && opts->include_large) ? 1 : 0);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: apps scan failed (%s)\n",
                wt_result_to_string(r));
        free(advice);
        return 1;
    }

    if (opts != NULL && wt_cli_is_json_mode(opts)) {
        wt_apps_print_json(advice);
    } else {
        wt_apps_print_text(advice);
    }
    free(advice);
    return 0;
}
