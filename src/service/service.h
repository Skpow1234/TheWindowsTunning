#ifndef WINTUNE_SERVICE_H
#define WINTUNE_SERVICE_H

#include <windows.h>

#include "common/error.h"

#define WT_SERVICE_NAME           L"WinTune"
#define WT_SERVICE_DISPLAY_NAME   L"WinTune Performance Agent"
#define WT_SERVICE_DESCRIPTION    \
    L"Local WinTune agent for privileged diagnostics and safe apply actions."

typedef enum WT_ServiceAccountKind {
    WT_SVC_ACCOUNT_LOCAL_SYSTEM = 0,
    WT_SVC_ACCOUNT_LOCAL_SERVICE,
    WT_SVC_ACCOUNT_VIRTUAL,
    WT_SVC_ACCOUNT_CUSTOM
} WT_ServiceAccountKind;

typedef struct WT_ServiceInstallOptions {
    int auto_start;
    WT_ServiceAccountKind account_kind;
    const wchar_t *custom_account;
    const wchar_t *custom_password;
} WT_ServiceInstallOptions;

typedef enum WT_ServiceInstallState {
    WT_SVC_INST_NOT_INSTALLED = 0,
    WT_SVC_INST_STOPPED,
    WT_SVC_INST_RUNNING,
    WT_SVC_INST_START_PENDING,
    WT_SVC_INST_STOP_PENDING
} WT_ServiceInstallState;

typedef struct WT_ServiceConfigInfo {
    WT_ServiceInstallState state;
    int pipe_reachable;
    int auto_start;
    WT_ServiceAccountKind account_kind;
    wchar_t account_name[256];
    wchar_t binary_path[1024];
} WT_ServiceConfigInfo;

int wt_service_run_dispatcher(void);

WT_Result wt_service_handle_request(const char *request_json,
                                    char **resp_out,
                                    size_t *resp_len);

WT_Result wt_service_install(const WT_ServiceInstallOptions *opts);
WT_Result wt_service_uninstall(void);
WT_Result wt_service_start(void);
WT_Result wt_service_stop(void);

WT_Result wt_service_query_state(WT_ServiceInstallState *state,
                                 int *pipe_reachable);

WT_Result wt_service_query_config(WT_ServiceConfigInfo *info);

const char *wt_service_account_kind_name(WT_ServiceAccountKind kind);

WT_Result wt_service_run_scan_and_cache(void);
unsigned wt_service_scan_interval_ms(void);

#endif /* WINTUNE_SERVICE_H */
