#include "actions/apply_preview.h"

#include "actions/rollback.h"
#include "system/startup.h"
#include "system/tasks.h"

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WT_APPROVED_RUN \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define WT_APPROVED_FOLDER \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"

void wt_apply_preview_init(WT_ApplyPreview *out)
{
    if (out == NULL) {
        return;
    }
    ZeroMemory(out, sizeof(*out));
}

static void wt_preview_set_rec(WT_ApplyPreview *out, const wchar_t *rec_id,
                               const wchar_t *target_id)
{
    if (rec_id != NULL) {
        StringCchCopyW(out->rec_id, ARRAYSIZE(out->rec_id), rec_id);
    }
    if (target_id != NULL) {
        StringCchCopyW(out->target_id, ARRAYSIZE(out->target_id), target_id);
    }
}

static WT_Result wt_startup_approved_location(WT_StartupSource src,
                                              const wchar_t **hive_tag,
                                              const wchar_t **subkey)
{
    switch (src) {
    case WT_STARTUP_SRC_HKCU_RUN:
        *hive_tag = L"HKCU";
        *subkey = WT_APPROVED_RUN;
        return WT_OK;
    case WT_STARTUP_SRC_HKLM_RUN:
        *hive_tag = L"HKLM";
        *subkey = WT_APPROVED_RUN;
        return WT_OK;
    case WT_STARTUP_SRC_USER_FOLDER:
        *hive_tag = L"HKCU";
        *subkey = WT_APPROVED_FOLDER;
        return WT_OK;
    case WT_STARTUP_SRC_COMMON_FOLDER:
        *hive_tag = L"HKLM";
        *subkey = WT_APPROVED_FOLDER;
        return WT_OK;
    default:
        return WT_ERR_NOT_SUPPORTED;
    }
}

static int wt_startup_read_enabled(const wchar_t *hive_tag,
                                   const wchar_t *subkey,
                                   const wchar_t *value_name)
{
    HKEY root = (_wcsicmp(hive_tag, L"HKLM") == 0) ? HKEY_LOCAL_MACHINE
                                                   : HKEY_CURRENT_USER;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return 1;
    }
    BYTE blob[12] = {0};
    DWORD size = sizeof(blob);
    DWORD type = 0;
    int enabled = 1;
    if (RegQueryValueExW(key, value_name, NULL, &type, blob, &size) == ERROR_SUCCESS
            && size >= 1) {
        enabled = ((blob[0] & 1) == 0);
    }
    RegCloseKey(key);
    return enabled;
}

static const WT_StartupEntry *wt_find_startup(const wchar_t *id,
                                              WT_StartupEntry *entries,
                                              size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (_wcsicmp(entries[i].id, id) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

static const WT_ScheduledTask *wt_find_task(const wchar_t *id,
                                            WT_ScheduledTask *tasks,
                                            size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (_wcsicmp(tasks[i].id, id) == 0) {
            return &tasks[i];
        }
    }
    return NULL;
}

WT_Result wt_apply_preview_power(WT_PowerScheme target, WT_ApplyPreview *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_apply_preview_init(out);
    if (target == WT_POWER_UNKNOWN) {
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Unknown power plan target.");
        return WT_ERR_INVALID_ARGUMENT;
    }

    if (target == WT_POWER_BALANCED || target == WT_POWER_POWER_SAVER) {
        wt_preview_set_rec(out, L"WT-POWER-002", NULL);
    } else {
        wt_preview_set_rec(out, L"WT-POWER-001", NULL);
    }
    StringCchCopyW(out->action_type, ARRAYSIZE(out->action_type),
                   WT_ROLLBACK_TYPE_POWER);
    out->rollback_available = 1;
    out->requires_admin = 0;

    WT_PowerInfo cur;
    if (wt_collect_power_info(&cur) != WT_OK) {
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Could not read the current power plan.");
        return WT_ERR_WIN32;
    }

    (void)wt_power_get_active_guid_string(out->previous_value,
                                          ARRAYSIZE(out->previous_value));
    if (wt_power_scheme_guid_string(target, out->new_value,
                                    ARRAYSIZE(out->new_value)) != WT_OK) {
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Could not resolve target power plan GUID.");
        return WT_ERR_INVALID_ARGUMENT;
    }

    StringCchPrintfW(out->description, ARRAYSIZE(out->description),
                     L"Power plan: %hs -> %hs",
                     wt_power_scheme_name(cur.scheme),
                     wt_power_scheme_name(target));

    if (cur.scheme == target) {
        out->would_mutate = 0;
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "Power plan is already '%s'. No change would be made.",
                         wt_power_scheme_name(target));
        return WT_OK;
    }

    out->would_mutate = 1;
    StringCchPrintfA(out->summary, sizeof(out->summary),
                     "Would switch power plan from '%s' to '%s'.",
                     wt_power_scheme_name(cur.scheme),
                     wt_power_scheme_name(target));
    return WT_OK;
}

WT_Result wt_apply_preview_startup_enabled(const wchar_t *id, int enable,
                                           WT_ApplyPreview *out)
{
    if (out == NULL || id == NULL || id[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_apply_preview_init(out);
    wt_preview_set_rec(out, L"WT-STARTUP-DISABLE", id);
    if (enable) {
        /* enable uses same mechanism; keep rec_id descriptive via summary */
        StringCchCopyW(out->rec_id, ARRAYSIZE(out->rec_id), L"startup-enable");
    }
    StringCchCopyW(out->action_type, ARRAYSIZE(out->action_type),
                   L"startup_approved");
    out->rollback_available = 1;

    WT_StartupEntry *entries =
        (WT_StartupEntry *)malloc(sizeof(WT_StartupEntry) * WT_MAX_STARTUP_ENTRIES);
    if (entries == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t count = 0;
    if (wt_collect_startup_entries(entries, WT_MAX_STARTUP_ENTRIES, &count) != WT_OK) {
        free(entries);
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Could not read startup entries.");
        return WT_ERR_WIN32;
    }

    const WT_StartupEntry *entry = wt_find_startup(id, entries, count);
    if (entry == NULL) {
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "No startup entry with id '%ls'.", id);
        free(entries);
        return WT_ERR_NOT_FOUND;
    }

    const wchar_t *hive_tag = NULL;
    const wchar_t *subkey = NULL;
    WT_Result loc = wt_startup_approved_location(entry->source, &hive_tag, &subkey);
    if (loc != WT_OK) {
        out->blocked = 1;
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "This startup entry's source is not supported for "
                       "enable/disable.");
        free(entries);
        return loc;
    }

    out->requires_admin = (_wcsicmp(hive_tag, L"HKLM") == 0) ? 1 : 0;
    int prev_enabled = wt_startup_read_enabled(hive_tag, subkey, entry->name);

    StringCchPrintfW(out->description, ARRAYSIZE(out->description),
                     L"Startup '%s': %s -> %s", entry->name,
                     prev_enabled ? L"enabled" : L"disabled",
                     enable ? L"enabled" : L"disabled");
    StringCchPrintfW(out->previous_value, ARRAYSIZE(out->previous_value),
                     L"%s|%s|%s|%s", hive_tag, subkey, entry->name,
                     prev_enabled ? L"02" : L"03");
    StringCchPrintfW(out->new_value, ARRAYSIZE(out->new_value),
                     L"%s|%s|%s|%s", hive_tag, subkey, entry->name,
                     enable ? L"02" : L"03");

    if ((enable && prev_enabled) || (!enable && !prev_enabled)) {
        out->would_mutate = 0;
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "Startup entry '%ls' is already %s.",
                         entry->name, enable ? "enabled" : "disabled");
        free(entries);
        return WT_OK;
    }

    out->would_mutate = 1;
    StringCchPrintfA(out->summary, sizeof(out->summary),
                     "Would %s startup entry '%ls'%s.",
                     enable ? "enable" : "disable", entry->name,
                     out->requires_admin ? " (requires administrator)" : "");
    free(entries);
    return WT_OK;
}

WT_Result wt_apply_preview_startup_delay(const wchar_t *id,
                                         unsigned long delay_seconds,
                                         WT_ApplyPreview *out)
{
    if (out == NULL || id == NULL || id[0] == L'\0' || delay_seconds == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_apply_preview_init(out);
    wt_preview_set_rec(out, L"WT-STARTUP-DELAY", id);
    StringCchCopyW(out->action_type, ARRAYSIZE(out->action_type),
                   L"startup_delay");
    out->rollback_available = 1;

    WT_StartupEntry *entries =
        (WT_StartupEntry *)malloc(sizeof(WT_StartupEntry) * WT_MAX_STARTUP_ENTRIES);
    if (entries == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t count = 0;
    if (wt_collect_startup_entries(entries, WT_MAX_STARTUP_ENTRIES, &count) != WT_OK) {
        free(entries);
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Could not read startup entries.");
        return WT_ERR_WIN32;
    }

    const WT_StartupEntry *entry = wt_find_startup(id, entries, count);
    if (entry == NULL) {
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "No startup entry with id '%ls'.", id);
        free(entries);
        return WT_ERR_NOT_FOUND;
    }

    const wchar_t *hive_tag = NULL;
    const wchar_t *subkey = NULL;
    WT_Result loc = wt_startup_approved_location(entry->source, &hive_tag, &subkey);
    if (loc != WT_OK) {
        out->blocked = 1;
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Delayed start is not supported for this startup source.");
        free(entries);
        return loc;
    }

    out->requires_admin = (_wcsicmp(hive_tag, L"HKLM") == 0) ? 1 : 0;
    StringCchPrintfW(out->description, ARRAYSIZE(out->description),
                     L"Startup delay '%s': %lu s", entry->name, delay_seconds);
    StringCchPrintfW(out->previous_value, ARRAYSIZE(out->previous_value),
                     L"%s|%s|%s|immediate", hive_tag, subkey, entry->name);
    StringCchPrintfW(out->new_value, ARRAYSIZE(out->new_value),
                     L"%s|%s|%s|delay:%lu", hive_tag, subkey, entry->name,
                     delay_seconds);
    out->would_mutate = 1;
    StringCchPrintfA(out->summary, sizeof(out->summary),
                     "Would delay startup entry '%ls' by %lu seconds%s.",
                     entry->name, delay_seconds,
                     out->requires_admin ? " (requires administrator)" : "");
    free(entries);
    return WT_OK;
}

WT_Result wt_apply_preview_task_enabled(const wchar_t *id, int enable,
                                        WT_ApplyPreview *out)
{
    if (out == NULL || id == NULL || id[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_apply_preview_init(out);
    wt_preview_set_rec(out, L"WT-TASK-DISABLE", id);
    StringCchCopyW(out->action_type, ARRAYSIZE(out->action_type),
                   L"task_enabled");
    out->rollback_available = 1;

    WT_ScheduledTask *tasks =
        (WT_ScheduledTask *)malloc(sizeof(WT_ScheduledTask) * WT_MAX_SCHEDULED_TASKS);
    if (tasks == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t count = 0;
    if (wt_collect_scheduled_tasks(tasks, WT_MAX_SCHEDULED_TASKS, &count,
                                   WT_TASK_FILTER_ALL) != WT_OK) {
        free(tasks);
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Could not read scheduled tasks.");
        return WT_ERR_WIN32;
    }

    const WT_ScheduledTask *task = wt_find_task(id, tasks, count);
    if (task == NULL) {
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "No scheduled task with id '%ls'.", id);
        free(tasks);
        return WT_ERR_NOT_FOUND;
    }
    if (wt_task_is_protected(task)) {
        out->blocked = 1;
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Refusing to change a protected Microsoft/security task.");
        free(tasks);
        return WT_ERR_NOT_SUPPORTED;
    }

    wchar_t path[256];
    if (wt_task_path_from_id(id, path, ARRAYSIZE(path)) != WT_OK) {
        free(tasks);
        return WT_ERR_INVALID_ARGUMENT;
    }

    StringCchPrintfW(out->description, ARRAYSIZE(out->description),
                     L"Task '%s': %s -> %s", task->name,
                     task->enabled ? L"enabled" : L"disabled",
                     enable ? L"enabled" : L"disabled");
    StringCchPrintfW(out->previous_value, ARRAYSIZE(out->previous_value),
                     L"%s|%s", path, task->enabled ? L"1" : L"0");
    StringCchPrintfW(out->new_value, ARRAYSIZE(out->new_value),
                     L"%s|%s", path, enable ? L"1" : L"0");

    if ((enable && task->enabled) || (!enable && !task->enabled)) {
        out->would_mutate = 0;
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "Task '%ls' is already %s.",
                         task->name, enable ? "enabled" : "disabled");
        free(tasks);
        return WT_OK;
    }

    out->would_mutate = 1;
    StringCchPrintfA(out->summary, sizeof(out->summary),
                     "Would %s scheduled task '%ls'.",
                     enable ? "enable" : "disable", task->name);
    free(tasks);
    return WT_OK;
}

WT_Result wt_apply_preview_task_delay(const wchar_t *id,
                                      unsigned long delay_seconds,
                                      WT_ApplyPreview *out)
{
    if (out == NULL || id == NULL || id[0] == L'\0' || delay_seconds == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_apply_preview_init(out);
    wt_preview_set_rec(out, L"WT-TASK-DELAY", id);
    StringCchCopyW(out->action_type, ARRAYSIZE(out->action_type), L"task_delay");
    out->rollback_available = 1;

    WT_ScheduledTask *tasks =
        (WT_ScheduledTask *)malloc(sizeof(WT_ScheduledTask) * WT_MAX_SCHEDULED_TASKS);
    if (tasks == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t count = 0;
    if (wt_collect_scheduled_tasks(tasks, WT_MAX_SCHEDULED_TASKS, &count,
                                   WT_TASK_FILTER_ALL) != WT_OK) {
        free(tasks);
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Could not read scheduled tasks.");
        return WT_ERR_WIN32;
    }

    const WT_ScheduledTask *task = wt_find_task(id, tasks, count);
    if (task == NULL) {
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "No scheduled task with id '%ls'.", id);
        free(tasks);
        return WT_ERR_NOT_FOUND;
    }
    if (wt_task_is_protected(task)) {
        out->blocked = 1;
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Refusing to change a protected Microsoft/security task.");
        free(tasks);
        return WT_ERR_NOT_SUPPORTED;
    }
    if (task->trigger_kind != WT_TASK_TRIGGER_LOGON) {
        out->blocked = 1;
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "Task '%ls' has no logon trigger; delay applies to "
                         "logon-triggered tasks only.",
                         task->name);
        free(tasks);
        return WT_ERR_NOT_SUPPORTED;
    }

    wchar_t path[256];
    if (wt_task_path_from_id(id, path, ARRAYSIZE(path)) != WT_OK) {
        free(tasks);
        return WT_ERR_INVALID_ARGUMENT;
    }

    StringCchPrintfW(out->description, ARRAYSIZE(out->description),
                     L"Task delay '%s': %lu s", task->name, delay_seconds);
    StringCchPrintfW(out->previous_value, ARRAYSIZE(out->previous_value),
                     L"%s|%lu", path, task->delay_seconds);
    StringCchPrintfW(out->new_value, ARRAYSIZE(out->new_value),
                     L"%s|%lu", path, delay_seconds);
    out->would_mutate = 1;
    StringCchPrintfA(out->summary, sizeof(out->summary),
                     "Would delay logon task '%ls' by %lu seconds.",
                     task->name, delay_seconds);
    free(tasks);
    return WT_OK;
}

WT_Result wt_apply_preview_from_request(const WT_ApplyRequest *req,
                                        WT_ApplyPreview *out)
{
    if (req == NULL || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_apply_preview_init(out);

    if (req->rec_id == NULL || req->rec_id[0] == L'\0') {
        StringCchCopyA(out->summary, sizeof(out->summary),
                       "Usage: wintune apply <id> [target-id] --dry-run");
        return WT_ERR_INVALID_ARGUMENT;
    }

    const WT_ApplySpec *spec = wt_apply_spec_lookup(req->rec_id);
    if (spec != NULL) {
        switch (spec->kind) {
        case WT_APPLY_POWER_SCHEME:
            return wt_apply_preview_power(spec->power_scheme, out);
        case WT_APPLY_STARTUP_DISABLE:
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchCopyA(out->summary, sizeof(out->summary),
                               "WT-STARTUP-DISABLE requires a startup entry id.");
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_apply_preview_startup_enabled(req->target_id, 0, out);
        case WT_APPLY_STARTUP_DELAY: {
            unsigned long delay = req->delay_seconds;
            if (delay == 0) {
                delay = spec->default_delay_seconds;
            }
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchCopyA(out->summary, sizeof(out->summary),
                               "WT-STARTUP-DELAY requires a startup entry id.");
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_apply_preview_startup_delay(req->target_id, delay, out);
        }
        case WT_APPLY_TASK_DISABLE:
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchCopyA(out->summary, sizeof(out->summary),
                               "WT-TASK-DISABLE requires a task id.");
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_apply_preview_task_enabled(req->target_id, 0, out);
        case WT_APPLY_TASK_DELAY: {
            unsigned long delay = req->delay_seconds;
            if (delay == 0) {
                delay = spec->default_delay_seconds;
            }
            if (req->target_id == NULL || req->target_id[0] == L'\0') {
                StringCchCopyA(out->summary, sizeof(out->summary),
                               "WT-TASK-DELAY requires a task id.");
                return WT_ERR_INVALID_ARGUMENT;
            }
            return wt_apply_preview_task_delay(req->target_id, delay, out);
        }
        default:
            break;
        }
    }

    if (wt_apply_is_advisory_rec_id(req->rec_id)) {
        wt_preview_set_rec(out, req->rec_id, req->target_id);
        out->would_mutate = 0;
        out->blocked = 1;
        StringCchPrintfA(out->summary, sizeof(out->summary),
                         "'%ls' is advisory: it has no automatic action.",
                         req->rec_id);
        return WT_ERR_NOT_SUPPORTED;
    }

    StringCchPrintfA(out->summary, sizeof(out->summary),
                     "'%ls' is not a recognized recommendation id.",
                     req->rec_id);
    return WT_ERR_NOT_FOUND;
}

static void wt_json_escape_w(FILE *out, const wchar_t *s)
{
    if (s == NULL) {
        return;
    }
    for (const wchar_t *p = s; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'"') {
            fputc('\\', out);
            fputc((char)*p, out);
        } else if (*p < 0x20) {
            fprintf(out, "\\u%04x", (unsigned)*p);
        } else if (*p < 0x80) {
            fputc((char)*p, out);
        } else {
            fprintf(out, "\\u%04x", (unsigned)*p);
        }
    }
}

static void wt_json_escape_a(FILE *out, const char *s)
{
    if (s == NULL) {
        return;
    }
    for (const char *p = s; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\' || c == '"') {
            fputc('\\', out);
            fputc((char)c, out);
        } else if (c < 0x20) {
            fprintf(out, "\\u%04x", c);
        } else {
            fputc((char)c, out);
        }
    }
}

void wt_apply_preview_print_text(FILE *out, const WT_ApplyPreview *p)
{
    if (out == NULL || p == NULL) {
        return;
    }
    fputs("WinTune Apply Preview (dry-run)\n\n", out);
    fprintf(out, "Action:      %ls\n",
            p->rec_id[0] != L'\0' ? p->rec_id : L"(none)");
    if (p->target_id[0] != L'\0') {
        fprintf(out, "Target:      %ls\n", p->target_id);
    }
    if (p->action_type[0] != L'\0') {
        fprintf(out, "Type:        %ls\n", p->action_type);
    }
    fprintf(out, "Would change: %s\n", p->would_mutate ? "yes" : "no");
    fprintf(out, "Blocked:      %s\n", p->blocked ? "yes" : "no");
    fprintf(out, "Requires admin: %s\n", p->requires_admin ? "yes" : "no");
    fprintf(out, "Rollback on apply: %s\n",
            p->rollback_available ? "yes" : "no");
    if (p->description[0] != L'\0') {
        fprintf(out, "Description: %ls\n", p->description);
    }
    if (p->previous_value[0] != L'\0') {
        fprintf(out, "Previous:    %ls\n", p->previous_value);
    }
    if (p->new_value[0] != L'\0') {
        fprintf(out, "New:         %ls\n", p->new_value);
    }
    if (p->summary[0] != '\0') {
        fprintf(out, "\n%s\n", p->summary);
    }
    fputs("\nNo system changes were made. Re-run without --dry-run to apply.\n",
          out);
}

void wt_apply_preview_print_json(FILE *out, const WT_ApplyPreview *p)
{
    if (out == NULL || p == NULL) {
        return;
    }
    fputs("{\n", out);
    fputs("  \"command\": \"apply\",\n", out);
    fputs("  \"dry_run\": true,\n", out);
    fputs("  \"rec_id\": \"", out);
    wt_json_escape_w(out, p->rec_id);
    fputs("\",\n", out);
    fputs("  \"target_id\": \"", out);
    wt_json_escape_w(out, p->target_id);
    fputs("\",\n", out);
    fputs("  \"action_type\": \"", out);
    wt_json_escape_w(out, p->action_type);
    fputs("\",\n", out);
    fputs("  \"description\": \"", out);
    wt_json_escape_w(out, p->description);
    fputs("\",\n", out);
    fputs("  \"previous_value\": \"", out);
    wt_json_escape_w(out, p->previous_value);
    fputs("\",\n", out);
    fputs("  \"new_value\": \"", out);
    wt_json_escape_w(out, p->new_value);
    fputs("\",\n", out);
    fputs("  \"summary\": \"", out);
    wt_json_escape_a(out, p->summary);
    fputs("\",\n", out);
    fprintf(out, "  \"would_mutate\": %s,\n",
            p->would_mutate ? "true" : "false");
    fprintf(out, "  \"blocked\": %s,\n", p->blocked ? "true" : "false");
    fprintf(out, "  \"requires_admin\": %s,\n",
            p->requires_admin ? "true" : "false");
    fprintf(out, "  \"rollback_available\": %s\n",
            p->rollback_available ? "true" : "false");
    fputs("}\n", out);
}
