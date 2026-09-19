#include "cli/commands_service.h"
#include "service/service.h"
#include "service/service_policy.h"
#include "platform/service_ipc.h"
#include "platform/service_client.h"
#include "system/privilege.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const char *wt_service_state_name(WT_ServiceInstallState st)
{
    switch (st) {
    case WT_SVC_INST_RUNNING:       return "running";
    case WT_SVC_INST_STOPPED:       return "stopped";
    case WT_SVC_INST_START_PENDING: return "starting";
    case WT_SVC_INST_STOP_PENDING:  return "stopping";
    default:                        return "not installed";
    }
}

static WT_Result wt_service_parse_install_options(const WT_CliOptions *cli,
                                                  WT_ServiceInstallOptions *out)
{
    if (cli == NULL || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    memset(out, 0, sizeof(*out));
    out->auto_start = cli->service_auto_start;
    out->account_kind = WT_SVC_ACCOUNT_LOCAL_SYSTEM;

    if (cli->service_account == NULL) {
        return WT_OK;
    }

    if (_wcsicmp(cli->service_account, L"system") == 0 ||
        _wcsicmp(cli->service_account, L"localsystem") == 0) {
        out->account_kind = WT_SVC_ACCOUNT_LOCAL_SYSTEM;
    } else if (_wcsicmp(cli->service_account, L"localservice") == 0 ||
               _wcsicmp(cli->service_account, L"local-service") == 0) {
        out->account_kind = WT_SVC_ACCOUNT_LOCAL_SERVICE;
    } else if (_wcsicmp(cli->service_account, L"virtual") == 0 ||
               _wcsicmp(cli->service_account, L"vsa") == 0) {
        out->account_kind = WT_SVC_ACCOUNT_VIRTUAL;
    } else {
        out->account_kind = WT_SVC_ACCOUNT_CUSTOM;
        out->custom_account = cli->service_account;
        out->custom_password = cli->service_account_password;
        if (out->custom_password == NULL) {
            return WT_ERR_INVALID_ARGUMENT;
        }
    }

    return WT_OK;
}

static void wt_service_print_policy(const WT_ServicePolicy *p, int json)
{
    if (p == NULL) {
        return;
    }
    if (json) {
        printf("  \"policy\": \"%s\",\n", p->name);
        printf("  \"scan_interval_ms\": %u,\n", p->scan_interval_ms);
        printf("  \"sample_count\": %u,\n", p->sample_count);
        printf("  \"top_process_limit\": %u,\n", p->top_process_limit);
        printf("  \"history_keep\": %u,\n", p->history_keep);
        printf("  \"policy_description\": \"%s\",\n",
               wt_service_policy_describe(p));
        return;
    }
    printf("  Policy:           %s\n", p->name);
    printf("  Description:      %s\n", wt_service_policy_describe(p));
    if (p->scan_interval_ms == 0) {
        printf("  Scan interval:    on-demand only\n");
    } else {
        printf("  Scan interval:    %u ms (%.1f min)\n", p->scan_interval_ms,
               (double)p->scan_interval_ms / 60000.0);
    }
    printf("  Samples / scan:   %u\n", p->sample_count);
    printf("  Top processes:    %u\n", p->top_process_limit);
    printf("  History keep:     %u prior last_scan file(s)\n", p->history_keep);
}

static int wt_service_cmd_status(const WT_CliOptions *opts)
{
    WT_ServiceConfigInfo info;
    WT_Result r = wt_service_query_config(&info);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: service status failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    WT_ServicePolicy policy;
    (void)wt_service_policy_load(&policy);
    WT_IpcAclMode pipe_acl = WT_IPC_ACL_ADMIN;
    (void)wt_service_ipc_acl_load(&pipe_acl);

    if (opts != NULL && opts->json) {
        printf("{\n");
        printf("  \"installed\": %s,\n",
               info.state == WT_SVC_INST_NOT_INSTALLED ? "false" : "true");
        printf("  \"state\": \"%s\",\n", wt_service_state_name(info.state));
        printf("  \"auto_start\": %s,\n", info.auto_start ? "true" : "false");
        printf("  \"account_kind\": \"%s\",\n",
               wt_service_account_kind_name(info.account_kind));
        printf("  \"account\": \"%ls\",\n", info.account_name);
        printf("  \"binary_path\": \"%ls\",\n", info.binary_path);
        printf("  \"pipe_reachable\": %s,\n",
               info.pipe_reachable ? "true" : "false");
        printf("  \"pipe\": \"%ls\",\n", WT_IPC_PIPE_NAME);
        printf("  \"pipe_acl\": \"%s\",\n", wt_service_ipc_acl_name(pipe_acl));
        printf("  \"pipe_acl_description\": \"%s\",\n",
               wt_service_ipc_acl_describe(pipe_acl));
        printf("  \"reject_remote_clients\": true,\n");
        wt_service_print_policy(&policy, 1);
        printf("  \"elevated_cli\": %s\n",
               wt_is_process_elevated() ? "true" : "false");
        printf("}\n");
        return 0;
    }

    printf("WinTune Service\n\n");
    printf("  Name:             %ls\n", WT_SERVICE_NAME);
    printf("  Display name:     %ls\n", WT_SERVICE_DISPLAY_NAME);
    printf("  Install state:    %s\n", wt_service_state_name(info.state));
    if (info.state != WT_SVC_INST_NOT_INSTALLED) {
        printf("  Start type:       %s\n",
               info.auto_start ? "Automatic" : "Manual (demand)");
        printf("  Run as:           %ls (%s)\n", info.account_name,
               wt_service_account_kind_name(info.account_kind));
        if (info.binary_path[0] != L'\0') {
            printf("  Binary path:      %ls\n", info.binary_path);
        }
    }
    printf("  Pipe:             %ls\n", WT_IPC_PIPE_NAME);
    printf("  Pipe reachable:   %s\n", info.pipe_reachable ? "yes" : "no");
    printf("  Pipe ACL:         %s\n", wt_service_ipc_acl_name(pipe_acl));
    printf("  Pipe ACL detail:  %s\n", wt_service_ipc_acl_describe(pipe_acl));
    printf("  Remote clients:   rejected\n");
    wt_service_print_policy(&policy, 0);
    printf("  CLI elevated:     %s\n",
           wt_is_process_elevated() ? "yes" : "no");
    printf("\nInstall (admin):  wintune service install [options]\n");
    printf("  --auto-start              start with Windows\n");
    printf("  --profile balanced|performance|light|on-demand\n");
    printf("  --pipe-acl admin|admin-only\n");
    printf("  --account system          Local System (default)\n");
    printf("  --account localservice    NT AUTHORITY\\LocalService\n");
    printf("  --account virtual         NT SERVICE\\WinTune (VSA)\n");
    printf("  --account DOMAIN\\User     custom account + --account-password\n");
    printf("Use via CLI:      wintune scan --via-service\n");
    return 0;
}

static int wt_service_cmd_install(const WT_CliOptions *opts)
{
    WT_ServiceInstallOptions install_opts;
    WT_Result r = wt_service_parse_install_options(opts, &install_opts);
    if (r == WT_ERR_INVALID_ARGUMENT) {
        fprintf(stderr,
                "wintune: custom --account requires --account-password\n");
        return 2;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: invalid service install options\n");
        return 2;
    }

    WT_ServicePolicy policy;
    r = wt_service_policy_from_name(
        opts != NULL ? opts->service_profile : NULL, &policy);
    if (r == WT_ERR_NOT_FOUND) {
        fprintf(stderr,
                "wintune: unknown --profile (use balanced|performance|light|"
                "on-demand)\n");
        return 2;
    }

    WT_IpcAclMode pipe_acl = WT_IPC_ACL_ADMIN;
    r = wt_service_ipc_acl_from_name(
        opts != NULL ? opts->service_pipe_acl : NULL, &pipe_acl);
    if (r == WT_ERR_NOT_FOUND) {
        fprintf(stderr,
                "wintune: unknown --pipe-acl (use admin|admin-only)\n");
        return 2;
    }

    r = wt_service_install(&install_opts);
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return 1;
    }
    if (r == WT_ERR_NOT_SUPPORTED) {
        fprintf(stderr, "wintune: service is already installed.\n");
        return 1;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: service install failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    r = wt_service_policy_save(&policy);
    if (r != WT_OK) {
        fprintf(stderr,
                "wintune: service installed, but could not write policy file "
                "(%s).\n"
                "Default balanced behavior will apply until: "
                "wintune service set-profile <name>\n",
                wt_result_to_string(r));
    }

    r = wt_service_ipc_acl_save(pipe_acl);
    if (r != WT_OK) {
        fprintf(stderr,
                "wintune: could not write pipe ACL file (%s). "
                "Default admin ACL applies until: "
                "wintune service set-pipe-acl <mode>\n",
                wt_result_to_string(r));
    }

    printf("WinTune service installed");
    if (install_opts.auto_start) {
        printf(" (automatic start)");
    } else {
        printf(" (manual start)");
    }
    printf(" as %s.\n",
           wt_service_account_kind_name(install_opts.account_kind));
    printf("Policy: %s — %s\n", policy.name, wt_service_policy_describe(&policy));
    printf("Pipe ACL: %s — %s\n", wt_service_ipc_acl_name(pipe_acl),
           wt_service_ipc_acl_describe(pipe_acl));
    printf("Start it with: wintune service start\n");
    printf("Note: restart the service after changing pipe ACL for new "
           "instances to use it.\n");
    return 0;
}

static int wt_service_cmd_set_profile(const WT_CliOptions *opts)
{
    const wchar_t *name = NULL;
    if (opts != NULL) {
        if (opts->arg2 != NULL && opts->arg2[0] != L'\0') {
            name = opts->arg2;
        } else {
            name = opts->service_profile;
        }
    }
    if (name == NULL || name[0] == L'\0') {
        fprintf(stderr,
                "Usage: wintune service set-profile "
                "<balanced|performance|light|on-demand>\n");
        return 2;
    }

    WT_ServicePolicy policy;
    WT_Result r = wt_service_policy_from_name(name, &policy);
    if (r != WT_OK) {
        fprintf(stderr,
                "wintune: unknown profile (use balanced|performance|light|"
                "on-demand)\n");
        return 2;
    }

    r = wt_service_policy_save(&policy);
    if (r == WT_ERR_ACCESS_DENIED || r == WT_ERR_WIN32) {
        if (!wt_is_process_elevated()) {
            wt_print_admin_required_message(stderr);
            return 1;
        }
        fprintf(stderr, "wintune: could not write policy (%s)\n",
                wt_result_to_string(r));
        return 1;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: set-profile failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    printf("Service policy set to %s — %s\n", policy.name,
           wt_service_policy_describe(&policy));
    printf("If the service is running, the next scan cycle picks up the new "
           "interval.\n");
    return 0;
}

static int wt_service_cmd_show_profile(const WT_CliOptions *opts)
{
    WT_ServicePolicy policy;
    (void)wt_service_policy_load(&policy);

    if (opts != NULL && opts->json) {
        printf("{\n");
        wt_service_print_policy(&policy, 1);
        printf("  \"profiles\": [\"balanced\",\"performance\",\"light\","
               "\"on-demand\"]\n");
        printf("}\n");
        return 0;
    }

    printf("WinTune service policy\n\n");
    wt_service_print_policy(&policy, 0);
    printf("\nAvailable profiles:\n");
    printf("  balanced      15 min interval, 3 samples (default)\n");
    printf("  performance   5 min interval, 5 samples\n");
    printf("  light         60 min interval, 1 sample\n");
    printf("  on-demand     no periodic scans (start + IPC only)\n");
    printf("\nChange: wintune service set-profile <name>  (admin to write "
           "ProgramData)\n");
    printf("Install: wintune service install --profile balanced\n");
    return 0;
}

static int wt_service_cmd_set_pipe_acl(const WT_CliOptions *opts)
{
    const wchar_t *name = NULL;
    if (opts != NULL) {
        if (opts->arg2 != NULL && opts->arg2[0] != L'\0') {
            name = opts->arg2;
        } else {
            name = opts->service_pipe_acl;
        }
    }
    if (name == NULL || name[0] == L'\0') {
        fprintf(stderr,
                "Usage: wintune service set-pipe-acl <admin|admin-only>\n");
        return 2;
    }

    WT_IpcAclMode mode = WT_IPC_ACL_ADMIN;
    WT_Result r = wt_service_ipc_acl_from_name(name, &mode);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: unknown pipe ACL (use admin|admin-only)\n");
        return 2;
    }

    r = wt_service_ipc_acl_save(mode);
    if (r != WT_OK) {
        if (!wt_is_process_elevated()) {
            wt_print_admin_required_message(stderr);
            return 1;
        }
        fprintf(stderr, "wintune: could not write pipe ACL (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    printf("Pipe ACL set to %s — %s\n", wt_service_ipc_acl_name(mode),
           wt_service_ipc_acl_describe(mode));
    printf("Restart the service so new pipe instances use the ACL:\n"
           "  wintune service stop && wintune service start\n");
    return 0;
}

static int wt_service_cmd_uninstall(void)
{
    WT_Result r = wt_service_uninstall();
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return 1;
    }
    if (r == WT_ERR_NOT_FOUND) {
        fprintf(stderr, "wintune: service is not installed.\n");
        return 1;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: service uninstall failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }
    printf("WinTune service uninstalled.\n");
    printf("Policy and pipe ACL files under %%ProgramData%%\\WinTune removed "
           "when present.\n");
    return 0;
}

static int wt_service_cmd_start(void)
{
    WT_Result r = wt_service_start();
    if (r == WT_ERR_NOT_FOUND) {
        fprintf(stderr, "wintune: service is not installed.\n");
        return 1;
    }
    if (r == WT_ERR_ACCESS_DENIED) {
        wt_print_admin_required_message(stderr);
        return 1;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: service start failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }
    printf("WinTune service started.\n");
    return 0;
}

static int wt_service_cmd_stop(void)
{
    WT_Result r = wt_service_stop();
    if (r == WT_ERR_NOT_FOUND) {
        fprintf(stderr, "wintune: service is not installed.\n");
        return 1;
    }
    if (r != WT_OK) {
        fprintf(stderr, "wintune: service stop failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }
    printf("WinTune service stopped.\n");
    return 0;
}

int wt_cmd_service(const WT_CliOptions *opts)
{
    if (opts == NULL || opts->arg1 == NULL) {
        fprintf(stderr,
                "Usage:\n"
                "  wintune service status\n"
                "  wintune service install     (admin)\n"
                "    [--auto-start]\n"
                "    [--profile balanced|performance|light|on-demand]\n"
                "    [--pipe-acl admin|admin-only]\n"
                "    [--account system|localservice|virtual|DOMAIN\\User]\n"
                "    [--account-password <secret>]  (required for custom)\n"
                "  wintune service profile\n"
                "  wintune service set-profile <name>  (admin to write)\n"
                "  wintune service set-pipe-acl <admin|admin-only>\n"
                "  wintune service uninstall   (admin)\n"
                "  wintune service start\n"
                "  wintune service stop\n");
        return 2;
    }

    if (wcscmp(opts->arg1, L"status") == 0) {
        return wt_service_cmd_status(opts);
    }
    if (wcscmp(opts->arg1, L"install") == 0) {
        return wt_service_cmd_install(opts);
    }
    if (wcscmp(opts->arg1, L"profile") == 0) {
        return wt_service_cmd_show_profile(opts);
    }
    if (wcscmp(opts->arg1, L"set-profile") == 0) {
        return wt_service_cmd_set_profile(opts);
    }
    if (wcscmp(opts->arg1, L"set-pipe-acl") == 0) {
        return wt_service_cmd_set_pipe_acl(opts);
    }
    if (wcscmp(opts->arg1, L"uninstall") == 0) {
        return wt_service_cmd_uninstall();
    }
    if (wcscmp(opts->arg1, L"start") == 0) {
        return wt_service_cmd_start();
    }
    if (wcscmp(opts->arg1, L"stop") == 0) {
        return wt_service_cmd_stop();
    }

    fwprintf(stderr,
             L"wintune: unknown service subcommand '%ls'.\n", opts->arg1);
    return 2;
}
