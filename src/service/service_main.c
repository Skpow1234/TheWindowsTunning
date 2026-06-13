#include "service/service.h"

#include "platform/paths.h"
#include "platform/service_ipc.h"
#include "common/log.h"

#include <windows.h>
#include <stdlib.h>

static SERVICE_STATUS g_ss;
static SERVICE_STATUS_HANDLE g_ssh;
static HANDLE g_stop_event;
static HANDLE g_scan_timer;
static volatile LONG g_running = 1;

static void wt_service_set_state(DWORD state, DWORD controls)
{
    g_ss.dwCurrentState = state;
    g_ss.dwControlsAccepted = controls;
    if (g_ssh != NULL) {
        SetServiceStatus(g_ssh, &g_ss);
    }
}

static void wt_service_unblock_pipe(void)
{
    HANDLE h = CreateFileW(WT_IPC_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

static void wt_service_handle_pipe(HANDLE pipe)
{
    char *req = NULL;
    size_t req_len = 0;
    WT_Result rr = wt_service_ipc_read_request(pipe, &req, &req_len);
    if (rr == WT_OK) {
        char *resp = NULL;
        size_t resp_len = 0;
        rr = wt_service_handle_request(req, &resp, &resp_len);
        if (rr == WT_OK && resp != NULL) {
            (void)wt_service_ipc_send_response(pipe, resp, resp_len);
            free(resp);
        } else {
            const char *errbody = "{\"ok\":false,\"error\":\"handler failed\"}";
            (void)wt_service_ipc_send_response(pipe, errbody, 0);
        }
    }
    free(req);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
}

static DWORD WINAPI wt_service_worker(LPVOID param)
{
    (void)param;

    wchar_t dir[MAX_PATH];
    if (wt_paths_program_data_dir(dir, ARRAYSIZE(dir)) == WT_OK) {
        (void)wt_paths_ensure_dir(dir);
    }

    (void)wt_service_run_scan_and_cache();

    g_scan_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (g_scan_timer != NULL) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)wt_service_scan_interval_ms() * 10000LL;
        (void)SetWaitableTimer(g_scan_timer, &due,
                               (LONG)wt_service_scan_interval_ms(), NULL, NULL, FALSE);
    }

    while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
        if (g_scan_timer != NULL &&
            WaitForSingleObject(g_scan_timer, 0) == WAIT_OBJECT_0) {
            (void)wt_service_run_scan_and_cache();
        }

        HANDLE pipe = INVALID_HANDLE_VALUE;
        WT_Result pr = wt_service_ipc_create_pipe_instance(&pipe);
        if (pr != WT_OK) {
            WT_LOGW("CreateNamedPipe failed");
            Sleep(1000);
            continue;
        }

        BOOL ok = ConnectNamedPipe(pipe, NULL);
        DWORD err = GetLastError();
        if (!ok && err != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            if (WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        wt_service_handle_pipe(pipe);
        CloseHandle(pipe);
    }

    if (g_scan_timer != NULL) {
        CloseHandle(g_scan_timer);
        g_scan_timer = NULL;
    }
    return 0;
}

static void WINAPI wt_service_ctrl_handler(DWORD control)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        InterlockedExchange(&g_running, 0);
        wt_service_set_state(SERVICE_STOP_PENDING, 0);
        if (g_stop_event != NULL) {
            SetEvent(g_stop_event);
        }
        wt_service_unblock_pipe();
    }
}

static void WINAPI wt_service_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    (void)argv;

    g_ssh = RegisterServiceCtrlHandlerW(WT_SERVICE_NAME, wt_service_ctrl_handler);
    if (g_ssh == NULL) {
        return;
    }

    g_ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ss.dwServiceSpecificExitCode = 0;
    wt_service_set_state(SERVICE_START_PENDING, 0);

    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_stop_event == NULL) {
        wt_service_set_state(SERVICE_STOPPED, 0);
        return;
    }

    wt_service_set_state(SERVICE_RUNNING,
                         SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);

    HANDLE thread = CreateThread(NULL, 0, wt_service_worker, NULL, 0, NULL);
    if (thread == NULL) {
        wt_service_set_state(SERVICE_STOPPED, 0);
        CloseHandle(g_stop_event);
        return;
    }

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    CloseHandle(g_stop_event);
    g_stop_event = NULL;

    wt_service_set_state(SERVICE_STOPPED, 0);
}

int wt_service_run_dispatcher(void)
{
    SERVICE_TABLE_ENTRYW table[] = {
        { (LPWSTR)WT_SERVICE_NAME, wt_service_main },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcherW(table)) {
        return (int)GetLastError();
    }
    return 0;
}
