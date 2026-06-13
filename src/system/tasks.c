#include "system/tasks.h"
#include "system/boot.h"

#include <windows.h>
#include <taskschd.h>
#include <strsafe.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef struct WT_ComScope {
    int initialized;
} WT_ComScope;

static WT_Result wt_com_begin(WT_ComScope *scope)
{
    if (scope == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    scope->initialized = 0;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        return WT_OK;
    }
    if (FAILED(hr)) {
        return WT_ERR_WIN32;
    }
    scope->initialized = 1;
    return WT_OK;
}

static void wt_com_end(WT_ComScope *scope)
{
    if (scope != NULL && scope->initialized) {
        CoUninitialize();
        scope->initialized = 0;
    }
}

static void wt_bstr_copy_w(wchar_t *out, size_t out_count, BSTR b)
{
    if (out == NULL || out_count == 0) {
        return;
    }
    out[0] = L'\0';
    if (b == NULL) {
        return;
    }
    StringCchCopyW(out, out_count, b);
}

static unsigned long wt_parse_iso8601_delay_seconds(BSTR delay)
{
    if (delay == NULL || delay[0] == L'\0') {
        return 0;
    }
    /* Task Scheduler uses ISO 8601 durations, commonly PT30S or PT1M. */
    const wchar_t *p = delay;
    if (wcsncmp(p, L"PT", 2) == 0) {
        p += 2;
    }
    unsigned long total = 0;
    while (*p != L'\0') {
        unsigned long n = 0;
        while (*p >= L'0' && *p <= L'9') {
            n = n * 10u + (unsigned long)(*p - L'0');
            p++;
        }
        if (*p == L'H') {
            total += n * 3600u;
        } else if (*p == L'M') {
            total += n * 60u;
        } else if (*p == L'S') {
            total += n;
        } else {
            break;
        }
        p++;
    }
    return total;
}

static WT_StartupImpact wt_task_estimate_impact(const wchar_t *name,
                                                const wchar_t *command)
{
    static const wchar_t *heavy[] = {
        L"update", L"backup", L"sync", L"index", L"google", L"adobe",
        L"steam", L"docker", L"onedrive", L"dropbox", L"java"
    };
    wchar_t haystack[900];
    haystack[0] = L'\0';
    StringCchCatW(haystack, ARRAYSIZE(haystack), name ? name : L"");
    StringCchCatW(haystack, ARRAYSIZE(haystack), L" ");
    StringCchCatW(haystack, ARRAYSIZE(haystack), command ? command : L"");
    CharLowerW(haystack);

    for (size_t i = 0; i < ARRAYSIZE(heavy); ++i) {
        if (wcsstr(haystack, heavy[i]) != NULL) {
            return WT_STARTUP_IMPACT_MEDIUM;
        }
    }
    return WT_STARTUP_IMPACT_UNKNOWN;
}

static int wt_task_path_is_microsoft(const wchar_t *path, const wchar_t *author)
{
    if (path != NULL && wcsstr(path, L"\\Microsoft\\") != NULL) {
        return 1;
    }
    if (author != NULL) {
        wchar_t lower[160];
        StringCchCopyW(lower, ARRAYSIZE(lower), author);
        CharLowerW(lower);
        if (wcsstr(lower, L"microsoft") != NULL) {
            return 1;
        }
    }
    return 0;
}

int wt_task_is_protected(const WT_ScheduledTask *task)
{
    if (task == NULL) {
        return 1;
    }
    if (task->is_microsoft) {
        return 1;
    }

    static const wchar_t *blocked_fragments[] = {
        L"Defender", L"WindowsUpdate", L"UpdateOrchestrator", L"WinDefend",
        L"SecurityHealth", L"Sense", L"ExploitGuard", L"Firewall",
        L"BitLocker", L"SmartScreen", L"UsoClient", L"WaaSMedic",
        L"MpCmdRun", L"Schedule Scan", L"Windows Defender"
    };

    wchar_t haystack[640];
    haystack[0] = L'\0';
    StringCchCatW(haystack, ARRAYSIZE(haystack), task->path);
    StringCchCatW(haystack, ARRAYSIZE(haystack), L" ");
    StringCchCatW(haystack, ARRAYSIZE(haystack), task->name);
    StringCchCatW(haystack, ARRAYSIZE(haystack), L" ");
    StringCchCatW(haystack, ARRAYSIZE(haystack), task->command);

    for (size_t i = 0; i < ARRAYSIZE(blocked_fragments); ++i) {
        if (wcsstr(haystack, blocked_fragments[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

const char *wt_task_trigger_name(WT_TaskTriggerKind kind)
{
    switch (kind) {
    case WT_TASK_TRIGGER_BOOT:  return "boot";
    case WT_TASK_TRIGGER_LOGON: return "logon";
    case WT_TASK_TRIGGER_OTHER: return "other";
    default:                    return "none";
    }
}

static int wt_task_trigger_matches_filter(TASK_TRIGGER_TYPE2 type,
                                          WT_TaskCollectFilter filter,
                                          WT_TaskTriggerKind *out_kind)
{
    WT_TaskTriggerKind kind = WT_TASK_TRIGGER_NONE;
    if (type == TASK_TRIGGER_BOOT) {
        kind = WT_TASK_TRIGGER_BOOT;
    } else if (type == TASK_TRIGGER_LOGON) {
        kind = WT_TASK_TRIGGER_LOGON;
    } else {
        kind = WT_TASK_TRIGGER_OTHER;
    }
    if (out_kind != NULL) {
        *out_kind = kind;
    }

    switch (filter) {
    case WT_TASK_FILTER_ALL:
        return 1;
    case WT_TASK_FILTER_LOGON:
        return (kind == WT_TASK_TRIGGER_LOGON);
    case WT_TASK_FILTER_STARTUP:
    default:
        return (kind == WT_TASK_TRIGGER_BOOT || kind == WT_TASK_TRIGGER_LOGON);
    }
}

static void wt_task_read_exec_action(IAction *action, wchar_t *command,
                                     size_t command_count)
{
    if (command == NULL || command_count == 0) {
        return;
    }
    command[0] = L'\0';
    if (action == NULL) {
        return;
    }

    IExecAction *exec = NULL;
    if (FAILED(action->QueryInterface(&IID_IExecAction, (void **)&exec))) {
        return;
    }

    BSTR path = NULL;
    BSTR args = NULL;
    if (SUCCEEDED(exec->lpVtbl->get_Path(exec, &path)) && path != NULL) {
        StringCchCopyW(command, command_count, path);
    }
    if (SUCCEEDED(exec->lpVtbl->get_Arguments(exec, &args)) && args != NULL &&
        args[0] != L'\0') {
        if (command[0] != L'\0') {
            StringCchCatW(command, command_count, L" ");
        }
        StringCchCatW(command, command_count, args);
    }
    if (path != NULL) {
        SysFreeString(path);
    }
    if (args != NULL) {
        SysFreeString(args);
    }
    exec->lpVtbl->Release(exec);
}

static void wt_task_read_triggers(ITaskDefinition *def,
                                  WT_TaskTriggerKind *best_kind,
                                  unsigned long *delay_seconds,
                                  int *matches_filter,
                                  WT_TaskCollectFilter filter)
{
    *best_kind = WT_TASK_TRIGGER_NONE;
    *delay_seconds = 0;
    *matches_filter = 0;

    ITriggerCollection *triggers = NULL;
    if (FAILED(def->lpVtbl->get_Triggers(def, &triggers)) ||
        triggers == NULL) {
        return;
    }

    LONG count = 0;
    triggers->lpVtbl->get_Count(triggers, &count);
    for (LONG i = 1; i <= count; ++i) {
        ITrigger *trigger = NULL;
        if (FAILED(triggers->lpVtbl->get_Item(triggers, i, &trigger)) ||
            trigger == NULL) {
            continue;
        }

        TASK_TRIGGER_TYPE2 type = TASK_TRIGGER_EVENT;
        trigger->lpVtbl->get_Type(trigger, &type);

        WT_TaskTriggerKind kind = WT_TASK_TRIGGER_NONE;
        if (wt_task_trigger_matches_filter(type, filter, &kind)) {
            *matches_filter = 1;
            if (*best_kind == WT_TASK_TRIGGER_NONE ||
                kind == WT_TASK_TRIGGER_LOGON) {
                *best_kind = kind;
            }
        }

        if (type == TASK_TRIGGER_LOGON) {
            ILogonTrigger *logon = NULL;
            if (SUCCEEDED(trigger->QueryInterface(
                    &IID_ILogonTrigger, (void **)&logon)) &&
                logon != NULL) {
                BSTR delay = NULL;
                if (SUCCEEDED(logon->lpVtbl->get_Delay(logon, &delay))) {
                    unsigned long sec = wt_parse_iso8601_delay_seconds(delay);
                    if (sec > *delay_seconds) {
                        *delay_seconds = sec;
                    }
                    if (delay != NULL) {
                        SysFreeString(delay);
                    }
                }
                logon->lpVtbl->Release(logon);
            }
        }

        trigger->lpVtbl->Release(trigger);
    }

    triggers->lpVtbl->Release(triggers);
}

static void wt_task_add(WT_ScheduledTask *out, size_t capacity, size_t *count,
                        const wchar_t *path, const wchar_t *name,
                        const wchar_t *command, const wchar_t *author,
                        int enabled, WT_TaskTriggerKind trigger_kind,
                        unsigned long delay_seconds)
{
    if (*count >= capacity || path == NULL || name == NULL) {
        return;
    }

    WT_ScheduledTask *t = &out[*count];
    ZeroMemory(t, sizeof(*t));
    StringCchCopyW(t->name, ARRAYSIZE(t->name), name);
    StringCchCopyW(t->path, ARRAYSIZE(t->path), path);
    StringCchCopyW(t->command, ARRAYSIZE(t->command), command ? command : L"");
    StringCchCopyW(t->author, ARRAYSIZE(t->author), author ? author : L"");
    StringCchPrintfW(t->id, ARRAYSIZE(t->id), L"task:%s", path);
    t->enabled = enabled;
    t->trigger_kind = trigger_kind;
    t->delay_seconds = delay_seconds;
    t->impact = wt_task_estimate_impact(t->name, t->command);
    t->is_microsoft = wt_task_path_is_microsoft(path, author);
    (*count)++;
}

static void wt_task_collect_registered(ITaskFolder *folder,
                                       const wchar_t *folder_path,
                                       WT_ScheduledTask *out, size_t capacity,
                                       size_t *count, WT_TaskCollectFilter filter)
{
    IRegisteredTaskCollection *tasks = NULL;
    if (FAILED(folder->lpVtbl->GetTasks(folder, TASK_ENUM_HIDDEN, &tasks)) ||
        tasks == NULL) {
        return;
    }

    LONG task_count = 0;
    tasks->lpVtbl->get_Count(tasks, &task_count);
    for (LONG i = 1; i <= task_count && *count < capacity; ++i) {
        IRegisteredTask *task = NULL;
        if (FAILED(tasks->lpVtbl->get_Item(tasks, (VARIANT){.vt = VT_I4, .lVal = i},
                                            &task)) ||
            task == NULL) {
            continue;
        }

        BSTR task_name = NULL;
        BSTR task_path = NULL;
        task->lpVtbl->get_Name(task, &task_name);
        task->lpVtbl->get_Path(task, &task_path);

        wchar_t full_path[256];
        if (task_path != NULL && task_path[0] != L'\0') {
            StringCchCopyW(full_path, ARRAYSIZE(full_path), task_path);
        } else if (task_name != NULL) {
            StringCchPrintfW(full_path, ARRAYSIZE(full_path), L"%s\\%s",
                             folder_path, task_name);
        } else {
            full_path[0] = L'\0';
        }

        ITaskDefinition *def = NULL;
        task->lpVtbl->get_Definition(task, &def);

        wchar_t command[512] = {0};
        wchar_t author[128] = {0};
        int enabled = 1;
        WT_TaskTriggerKind trigger_kind = WT_TASK_TRIGGER_NONE;
        unsigned long delay_seconds = 0;
        int matches = 0;

        if (def != NULL) {
            IRegistrationInfo *info = NULL;
            if (SUCCEEDED(def->lpVtbl->get_RegistrationInfo(def, &info)) &&
                info != NULL) {
                BSTR auth = NULL;
                if (SUCCEEDED(info->lpVtbl->get_Author(info, &auth))) {
                    wt_bstr_copy_w(author, ARRAYSIZE(author), auth);
                    if (auth != NULL) {
                        SysFreeString(auth);
                    }
                }
                info->lpVtbl->Release(info);
            }

            ITaskSettings *settings = NULL;
            if (SUCCEEDED(def->lpVtbl->get_Settings(def, &settings)) &&
                settings != NULL) {
                VARIANT_BOOL on = VARIANT_TRUE;
                if (SUCCEEDED(settings->lpVtbl->get_Enabled(settings, &on))) {
                    enabled = (on == VARIANT_TRUE) ? 1 : 0;
                }
                settings->lpVtbl->Release(settings);
            }

            IActionCollection *actions = NULL;
            if (SUCCEEDED(def->lpVtbl->get_Actions(def, &actions)) &&
                actions != NULL) {
                IAction *action = NULL;
                if (SUCCEEDED(actions->lpVtbl->get_Item(actions, 1, &action))) {
                    wt_task_read_exec_action(action, command, ARRAYSIZE(command));
                    action->lpVtbl->Release(action);
                }
                actions->lpVtbl->Release(actions);
            }

            wt_task_read_triggers(def, &trigger_kind, &delay_seconds, &matches,
                                  filter);
            def->lpVtbl->Release(def);
        }

        if (filter == WT_TASK_FILTER_ALL || matches) {
            wt_task_add(out, capacity, count, full_path,
                        task_name ? task_name : L"(unknown)",
                        command, author, enabled, trigger_kind, delay_seconds);
        }

        if (task_name != NULL) {
            SysFreeString(task_name);
        }
        if (task_path != NULL) {
            SysFreeString(task_path);
        }
        task->lpVtbl->Release(task);
    }

    tasks->lpVtbl->Release(tasks);
}

static void wt_task_collect_folders(ITaskFolder *folder, const wchar_t *folder_path,
                                    WT_ScheduledTask *out, size_t capacity,
                                    size_t *count, WT_TaskCollectFilter filter,
                                    int depth)
{
    if (depth > 24 || *count >= capacity) {
        return;
    }

    wt_task_collect_registered(folder, folder_path, out, capacity, count, filter);

    ITaskFolderCollection *subfolders = NULL;
    if (FAILED(folder->lpVtbl->GetFolders(folder, 0, &subfolders)) ||
        subfolders == NULL) {
        return;
    }

    LONG sub_count = 0;
    subfolders->lpVtbl->get_Count(subfolders, &sub_count);
    for (LONG i = 1; i <= sub_count && *count < capacity; ++i) {
        ITaskFolder *sub = NULL;
        VARIANT v = {.vt = VT_I4, .lVal = i};
        if (FAILED(subfolders->lpVtbl->get_Item(subfolders, v, &sub)) ||
            sub == NULL) {
            continue;
        }

        BSTR sub_name = NULL;
        sub->lpVtbl->get_Name(sub, &sub_name);

        wchar_t sub_path[256];
        if (sub_name != NULL && sub_name[0] != L'\0') {
            if (folder_path[0] == L'\\' && folder_path[1] == L'\0') {
                StringCchPrintfW(sub_path, ARRAYSIZE(sub_path), L"\\%s", sub_name);
            } else {
                StringCchPrintfW(sub_path, ARRAYSIZE(sub_path), L"%s\\%s",
                                 folder_path, sub_name);
            }
        } else {
            StringCchCopyW(sub_path, ARRAYSIZE(sub_path), folder_path);
        }

        wt_task_collect_folders(sub, sub_path, out, capacity, count, filter,
                                depth + 1);

        if (sub_name != NULL) {
            SysFreeString(sub_name);
        }
        sub->lpVtbl->Release(sub);
    }

    subfolders->lpVtbl->Release(subfolders);
}

WT_Result wt_collect_scheduled_tasks(WT_ScheduledTask *out,
                                     size_t capacity,
                                     size_t *out_count,
                                     WT_TaskCollectFilter filter)
{
    if (out == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    WT_ComScope com;
    WT_Result r = wt_com_begin(&com);
    if (r != WT_OK) {
        return r;
    }

    ITaskService *service = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_ITaskService, (void **)&service);
    if (FAILED(hr) || service == NULL) {
        wt_com_end(&com);
        return WT_ERR_WIN32;
    }

    VARIANT empty = {.vt = VT_EMPTY};
    hr = service->lpVtbl->Connect(service, empty, empty, empty, empty);
    if (FAILED(hr)) {
        service->lpVtbl->Release(service);
        wt_com_end(&com);
        return WT_ERR_ACCESS_DENIED;
    }

    ITaskFolder *root = NULL;
    hr = service->lpVtbl->GetFolder(service, L"\\", &root);
    if (FAILED(hr) || root == NULL) {
        service->lpVtbl->Release(service);
        wt_com_end(&com);
        return WT_ERR_WIN32;
    }

    wt_task_collect_folders(root, L"\\", out, capacity, out_count, filter, 0);

    root->lpVtbl->Release(root);
    service->lpVtbl->Release(service);
    wt_com_end(&com);
    return WT_OK;
}

void wt_tasks_apply_measured(WT_ScheduledTask *tasks, size_t count,
                             const WT_BootReport *boot)
{
    if (tasks == NULL || boot == NULL) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        WT_ScheduledTask *t = &tasks[i];
        t->measured_ms = 0;
        t->measured_available = 0;

        unsigned long ms = 0;
        int matched = 0;
        wt_boot_apply_measured_startup(boot, t->name, t->command, &ms, &matched);
        if (!matched) {
            continue;
        }

        t->measured_ms = ms;
        t->measured_available = 1;
        if (ms >= 10000) {
            t->impact = WT_STARTUP_IMPACT_HIGH;
        } else if (ms >= 3000) {
            t->impact = WT_STARTUP_IMPACT_MEDIUM;
        } else if (ms > 0) {
            t->impact = WT_STARTUP_IMPACT_LOW;
        }
    }
}
