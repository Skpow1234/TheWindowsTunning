#include "cli/commands_service.h"
#include "service/service.h"
#include "platform/service_ipc.h"
#include "platform/service_client.h"
#include "system/privilege.h"

#include <stdio.h>

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

static int wt_service_cmd_status(const WT_CliOptions *opts)
{
    WT_ServiceInstallState st = WT_SVC_INST_NOT_INSTALLED;
    int pipe_ok = 0;
    WT_Result r = wt_service_query_state(&st, &pipe_ok);
    if (r != WT_OK) {
        fprintf(stderr, "wintune: service status failed (%s)\n",
                wt_result_to_string(r));
        return 1;
    }

    if (opts != NULL && opts->json) {
        printf("{\n");
        printf("  \"installed\": %s,\n",
               st == WT_SVC_INST_NOT_INSTALLED ? "false" : "true");
        printf("  \"state\": \"%s\",\n", wt_service_state_name(st));
        printf("  \"pipe_reachable\": %s,\n", pipe_ok ? "true" : "false");
        printf("  \"pipe\": \"%ls\",\n", WT_IPC_PIPE_NAME);
        printf("  \"elevated_cli\": %s\n",
               wt_is_process_elevated() ? "true" : "false");
        printf("}\n");
        return 0;
    }

    printf("WinTune Service\n\n");
    printf("  Name:             %ls\n", WT_SERVICE_NAME);
    printf("  Display name:     %ls\n", WT_SERVICE_DISPLAY_NAME);
    printf("  Install state:    %s\n", wt_service_state_name(st));
    printf("  Pipe:             %ls\n", WT_IPC_PIPE_NAME);
    printf("  Pipe reachable:   %s\n", pipe_ok ? "yes" : "no");
    printf("  CLI elevated:     %s\n",
           wt_is_process_elevated() ? "yes" : "no");
    printf("\nInstall (admin):  wintune service install\n");
    printf("Start:            wintune service start\n");
    printf("Use via CLI:      wintune scan --via-service\n");
    return 0;
}

static int wt_service_cmd_install(void)
{
    WT_Result r = wt_service_install();
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
    printf("WinTune service installed (demand start).\n");
    printf("Start it with: wintune service start\n");
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
                "  wintune service uninstall   (admin)\n"
                "  wintune service start\n"
                "  wintune service stop\n");
        return 2;
    }

    if (wcscmp(opts->arg1, L"status") == 0) {
        return wt_service_cmd_status(opts);
    }
    if (wcscmp(opts->arg1, L"install") == 0) {
        return wt_service_cmd_install();
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
