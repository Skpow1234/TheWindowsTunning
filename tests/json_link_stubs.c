/* Minimal stubs so test_json can link output/json.c without the full app. */
#include "core/recommendations.h"
#include "system/boot.h"
#include "system/startup.h"
#include "system/tasks.h"
#include "system/services.h"
#include "system/updates.h"

#include <stddef.h>

const char *wt_boot_component_kind_name(WT_BootComponentKind kind)
{
    (void)kind;
    return "unknown";
}

WT_Result wt_format_filetime_iso8601_utc(const FILETIME *ft, char *out,
                                         size_t out_cap)
{
    (void)ft;
    if (out == NULL || out_cap == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out[0] = '\0';
    return WT_OK;
}

const char *wt_severity_to_string(WT_Severity severity)
{
    (void)severity;
    return "info";
}

const char *wt_risk_to_string(WT_Risk risk)
{
    (void)risk;
    return "none";
}

const char *wt_startup_source_name(WT_StartupSource source)
{
    (void)source;
    return "unknown";
}

const char *wt_startup_impact_name(WT_StartupImpact impact)
{
    (void)impact;
    return "unknown";
}

const char *wt_task_trigger_name(WT_TaskTriggerKind kind)
{
    (void)kind;
    return "unknown";
}

const char *wt_service_state_name(WT_ServiceState state)
{
    (void)state;
    return "unknown";
}

const char *wt_service_start_type_name(WT_ServiceStartType type)
{
    (void)type;
    return "unknown";
}
