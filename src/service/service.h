#ifndef WINTUNE_SERVICE_H
#define WINTUNE_SERVICE_H

#include <windows.h>

#include "common/error.h"

#define WT_SERVICE_NAME           L"WinTune"
#define WT_SERVICE_DISPLAY_NAME   L"WinTune Performance Agent"
#define WT_SERVICE_DESCRIPTION    \
    L"Local WinTune agent for privileged diagnostics and safe apply actions."

/* SCM entry: wintune.exe service run */
int wt_service_run_dispatcher(void);

/* Handles one IPC request payload and writes a response payload (caller frees
 * *resp_out with free()). */
WT_Result wt_service_handle_request(const char *request_json,
                                    char **resp_out,
                                    size_t *resp_len);

WT_Result wt_service_install(void);
WT_Result wt_service_uninstall(void);
WT_Result wt_service_start(void);
WT_Result wt_service_stop(void);

typedef enum WT_ServiceInstallState {
    WT_SVC_INST_NOT_INSTALLED = 0,
    WT_SVC_INST_STOPPED,
    WT_SVC_INST_RUNNING,
    WT_SVC_INST_START_PENDING,
    WT_SVC_INST_STOP_PENDING
} WT_ServiceInstallState;

WT_Result wt_service_query_state(WT_ServiceInstallState *state,
                                 int *pipe_reachable);

WT_Result wt_service_run_scan_and_cache(void);
unsigned wt_service_scan_interval_ms(void);

#endif /* WINTUNE_SERVICE_H */
