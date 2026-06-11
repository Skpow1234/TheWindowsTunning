#ifndef WINTUNE_SERVICES_H
#define WINTUNE_SERVICES_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

typedef enum WT_ServiceState {
    WT_SVC_STATE_UNKNOWN = 0,
    WT_SVC_STATE_STOPPED,
    WT_SVC_STATE_START_PENDING,
    WT_SVC_STATE_STOP_PENDING,
    WT_SVC_STATE_RUNNING,
    WT_SVC_STATE_CONTINUE_PENDING,
    WT_SVC_STATE_PAUSE_PENDING,
    WT_SVC_STATE_PAUSED
} WT_ServiceState;

typedef enum WT_ServiceStartType {
    WT_SVC_START_UNKNOWN = 0,
    WT_SVC_START_BOOT,
    WT_SVC_START_SYSTEM,
    WT_SVC_START_AUTO,
    WT_SVC_START_DEMAND,
    WT_SVC_START_DISABLED
} WT_ServiceStartType;

typedef struct WT_ServiceInfo {
    wchar_t name[256];          /* service (key) name */
    wchar_t display_name[256];  /* friendly display name */
    WT_ServiceState state;
    WT_ServiceStartType start_type;
    unsigned long pid;          /* 0 if not running / unknown */
} WT_ServiceInfo;

#define WT_MAX_SERVICES 1024

/* Enumerates Win32 services (state + PID via the SCM, start type via per-service
 * config). Read-only. Requires no elevation for enumeration on typical systems;
 * a per-service config read that is denied leaves start_type as "unknown"
 * rather than failing the whole scan.
 *
 * Returns WT_ERR_ACCESS_DENIED if the SCM cannot be opened at all. */
WT_Result wt_collect_services(WT_ServiceInfo *out,
                              size_t capacity,
                              size_t *out_count);

const char *wt_service_state_name(WT_ServiceState state);
const char *wt_service_start_type_name(WT_ServiceStartType type);

#endif /* WINTUNE_SERVICES_H */
