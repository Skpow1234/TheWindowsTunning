#ifndef WINTUNE_TASKS_H
#define WINTUNE_TASKS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"
#include "system/startup.h"

typedef enum WT_TaskTriggerKind {
    WT_TASK_TRIGGER_NONE = 0,
    WT_TASK_TRIGGER_BOOT,
    WT_TASK_TRIGGER_LOGON,
    WT_TASK_TRIGGER_OTHER
} WT_TaskTriggerKind;

typedef struct WT_BootReport WT_BootReport;

typedef struct WT_ScheduledTask {
    wchar_t id[280];          /* task:<full path> */
    wchar_t name[128];
    wchar_t path[256];        /* Task Scheduler path, e.g. \Vendor\App */
    wchar_t command[512];
    wchar_t author[128];
    int enabled;
    WT_TaskTriggerKind trigger_kind;
    unsigned long delay_seconds; /* existing logon delay, 0 if none */
    WT_StartupImpact impact;
    int is_microsoft;         /* protected from automatic/mutating actions */
    unsigned long measured_ms;
    int measured_available;
} WT_ScheduledTask;

#define WT_MAX_SCHEDULED_TASKS 256

typedef enum WT_TaskCollectFilter {
    WT_TASK_FILTER_STARTUP = 0, /* boot + logon triggers only */
    WT_TASK_FILTER_LOGON,       /* logon triggers only */
    WT_TASK_FILTER_ALL          /* every task (can be large) */
} WT_TaskCollectFilter;

/* Enumerates scheduled tasks using the Task Scheduler COM API. Read-only.
 * Partial folder failures are skipped. */
WT_Result wt_collect_scheduled_tasks(WT_ScheduledTask *out,
                                     size_t capacity,
                                     size_t *out_count,
                                     WT_TaskCollectFilter filter);

const char *wt_task_trigger_name(WT_TaskTriggerKind kind);

/* Correlates tasks with measured boot/login component data. */
void wt_tasks_apply_measured(WT_ScheduledTask *tasks, size_t count,
                             const WT_BootReport *boot);

/* Returns 1 when WinTune should refuse disable/delay on this task. */
int wt_task_is_protected(const WT_ScheduledTask *task);

/* Strips the optional task: prefix and returns a Task Scheduler path. */
WT_Result wt_task_path_from_id(const wchar_t *id, wchar_t *path, size_t path_count);

WT_Result wt_task_set_enabled(const wchar_t *task_path, int enable);
WT_Result wt_task_set_logon_delay(const wchar_t *task_path,
                                  unsigned long delay_seconds);

#endif /* WINTUNE_TASKS_H */
