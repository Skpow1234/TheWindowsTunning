#include "actions/safe_actions.h"
#include "actions/rollback.h"
#include "system/power.h"
#include "system/privilege.h"
#include "system/startup.h"
#include "platform/console.h"
#include "common/log.h"

#include <windows.h>
#include <winsvc.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- confirmation -------------------------------------------------------- */

int wt_action_confirm(const char *prompt, int assume_yes)
{
    if (assume_yes) {
        return 1;
    }
    if (!wt_session_is_interactive()) {
        fprintf(stderr,
                "%s\nRefusing to proceed without confirmation in a "
                "non-interactive session. Re-run with --yes to confirm.",
                prompt);
        if (wt_session_is_remote()) {
            fputs("\nOver SSH one-shot commands, pass --yes explicitly "
                  "(e.g. ssh user@host \"wintune apply WT-POWER-001 --yes\").\n",
                  stderr);
        } else {
            fputc('\n', stderr);
        }
        return 0;
    }

    printf("%s [y/N]: ", prompt);
    fflush(stdout);

    char line[16] = {0};
    if (fgets(line, sizeof(line), stdin) == NULL) {
        return 0;
    }
    for (char *p = line; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t') {
            continue;
        }
        return (*p == 'y' || *p == 'Y') ? 1 : 0;
    }
    return 0;
}

/* ---- power plan ---------------------------------------------------------- */

WT_Result wt_action_set_power_plan(WT_PowerScheme target,
                                   int assume_yes,
                                   char *msg, size_t msg_cap)
{
    if (msg != NULL && msg_cap > 0) {
        msg[0] = '\0';
    }
    if (target == WT_POWER_UNKNOWN) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    WT_PowerInfo cur;
    if (wt_collect_power_info(&cur) != WT_OK) {
        StringCchPrintfA(msg, msg_cap, "Could not read the current power plan.");
        return WT_ERR_WIN32;
    }

    if (cur.scheme == target) {
        StringCchPrintfA(msg, msg_cap,
                         "Power plan is already '%s'. No change made.",
                         wt_power_scheme_name(target));
        return WT_OK;
    }

    wchar_t prev_guid[64] = {0};
    (void)wt_power_get_active_guid_string(prev_guid, ARRAYSIZE(prev_guid));

    char prompt[256];
    StringCchPrintfA(prompt, sizeof(prompt),
                     "Switch power plan from '%s' to '%s'?",
                     wt_power_scheme_name(cur.scheme),
                     wt_power_scheme_name(target));
    if (!wt_action_confirm(prompt, assume_yes)) {
        StringCchPrintfA(msg, msg_cap, "Cancelled. Power plan unchanged.");
        return WT_ERR_CANCELLED;
    }

    WT_Result r = wt_power_set_active_scheme(target);
    if (r == WT_ERR_NOT_FOUND) {
        StringCchPrintfA(msg, msg_cap,
                         "The '%s' power plan does not exist on this system.",
                         wt_power_scheme_name(target));
        return r;
    }
    if (r != WT_OK) {
        StringCchPrintfA(msg, msg_cap, "Failed to switch power plan (%s).",
                         wt_result_to_string(r));
        return r;
    }

    wchar_t new_guid[64] = {0};
    (void)wt_power_get_active_guid_string(new_guid, ARRAYSIZE(new_guid));

    WT_RollbackRecord rec;
    ZeroMemory(&rec, sizeof(rec));
    StringCchCopyW(rec.action_type, ARRAYSIZE(rec.action_type),
                   WT_ROLLBACK_TYPE_POWER);
    StringCchPrintfW(rec.description, ARRAYSIZE(rec.description),
                     L"Power plan: %hs -> %hs",
                     wt_power_scheme_name(cur.scheme),
                     wt_power_scheme_name(target));
    StringCchCopyW(rec.previous_value, ARRAYSIZE(rec.previous_value), prev_guid);
    StringCchCopyW(rec.new_value, ARRAYSIZE(rec.new_value), new_guid);

    WT_Result rr = wt_rollback_write(&rec);
    if (rr == WT_OK) {
        StringCchPrintfA(msg, msg_cap,
                         "Switched power plan to '%s'. Rollback id: %ls",
                         wt_power_scheme_name(target), rec.id);
    } else {
        WT_LOGW("could not write rollback record (%s)", wt_result_to_string(rr));
        StringCchPrintfA(msg, msg_cap,
                         "Switched power plan to '%s'. (rollback record not saved)",
                         wt_power_scheme_name(target));
    }
    return WT_OK;
}

/* ---- service restart ----------------------------------------------------- */

/* Services we refuse to restart: stopping these mid-session can destabilize
 * the OS or disrupt security. Conservative and case-insensitive. */
static int wt_service_is_protected(const wchar_t *name)
{
    static const wchar_t *protected_names[] = {
        L"RpcSs", L"DcomLaunch", L"RpcEptMapper", L"LSM", L"Power",
        L"WinDefend", L"MpsSvc", L"SecurityHealthService", L"wscsvc",
        L"CryptSvc", L"BFE", L"gpsvc", L"Schedule", L"ProfSvc",
        L"Themes", L"Winmgmt", L"EventLog", L"PlugPlay"
    };
    for (size_t i = 0; i < ARRAYSIZE(protected_names); ++i) {
        if (_wcsicmp(name, protected_names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int wt_service_wait_state(SC_HANDLE svc, DWORD desired, DWORD timeout_ms)
{
    DWORD waited = 0;
    SERVICE_STATUS_PROCESS ssp;
    DWORD needed = 0;
    while (waited < timeout_ms) {
        if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                                  (LPBYTE)&ssp, sizeof(ssp), &needed)) {
            return 0;
        }
        if (ssp.dwCurrentState == desired) {
            return 1;
        }
        Sleep(200);
        waited += 200;
    }
    return 0;
}

WT_Result wt_action_restart_service(const wchar_t *name,
                                    int assume_yes,
                                    char *msg, size_t msg_cap)
{
    if (msg != NULL && msg_cap > 0) {
        msg[0] = '\0';
    }
    if (name == NULL || name[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    if (wt_service_is_protected(name)) {
        StringCchPrintfA(msg, msg_cap,
                         "Refusing to restart '%ls': it is a critical/security "
                         "service and restarting it could destabilize Windows.",
                         name);
        return WT_ERR_NOT_SUPPORTED;
    }

    if (!wt_is_process_elevated()) {
        wt_print_admin_required_message(stderr);
        StringCchPrintfA(msg, msg_cap,
                         "Restarting a service requires administrator privileges.");
        return WT_ERR_ACCESS_DENIED;
    }

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        StringCchPrintfA(msg, msg_cap, "Could not open the Service Control Manager.");
        return WT_ERR_WIN32;
    }

    SC_HANDLE svc = OpenServiceW(scm, name,
                                 SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
    if (svc == NULL) {
        DWORD err = GetLastError();
        StringCchPrintfA(msg, msg_cap,
                         (err == ERROR_SERVICE_DOES_NOT_EXIST)
                             ? "No service named '%ls' exists."
                             : "Could not open service '%ls'.",
                         name);
        CloseServiceHandle(scm);
        return (err == ERROR_SERVICE_DOES_NOT_EXIST) ? WT_ERR_NOT_FOUND
                                                     : WT_ERR_WIN32;
    }

    char prompt[256];
    StringCchPrintfA(prompt, sizeof(prompt), "Restart service '%ls'?", name);
    if (!wt_action_confirm(prompt, assume_yes)) {
        StringCchPrintfA(msg, msg_cap, "Cancelled. Service '%ls' not restarted.", name);
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return WT_ERR_CANCELLED;
    }

    WT_Result result = WT_OK;
    SERVICE_STATUS_PROCESS ssp;
    DWORD needed = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                             (LPBYTE)&ssp, sizeof(ssp), &needed) &&
        ssp.dwCurrentState != SERVICE_STOPPED) {
        SERVICE_STATUS st;
        if (!ControlService(svc, SERVICE_CONTROL_STOP, &st)) {
            StringCchPrintfA(msg, msg_cap, "Failed to stop service '%ls'.", name);
            result = WT_ERR_WIN32;
        } else if (!wt_service_wait_state(svc, SERVICE_STOPPED, 15000)) {
            StringCchPrintfA(msg, msg_cap,
                             "Service '%ls' did not stop within the timeout.", name);
            result = WT_ERR_TIMEOUT;
        }
    }

    if (result == WT_OK) {
        if (!StartServiceW(svc, 0, NULL)) {
            StringCchPrintfA(msg, msg_cap, "Failed to start service '%ls'.", name);
            result = WT_ERR_WIN32;
        } else if (!wt_service_wait_state(svc, SERVICE_RUNNING, 15000)) {
            StringCchPrintfA(msg, msg_cap,
                             "Service '%ls' was started but is not yet running.", name);
            result = WT_ERR_TIMEOUT;
        } else {
            StringCchPrintfA(msg, msg_cap, "Service '%ls' restarted.", name);
        }
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return result;
}

/* ---- startup enable/disable (StartupApproved) ---------------------------- */

#define WT_APPROVED_RUN \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define WT_APPROVED_FOLDER \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder"

static HKEY wt_hive_from_tag(const wchar_t *tag)
{
    return (_wcsicmp(tag, L"HKLM") == 0) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

WT_Result wt_startup_write_approved(const wchar_t *hive_tag,
                                    const wchar_t *subkey,
                                    const wchar_t *value_name,
                                    int enabled)
{
    HKEY root = wt_hive_from_tag(hive_tag);
    HKEY key = NULL;
    LONG rc = RegCreateKeyExW(root, subkey, 0, NULL, 0,
                              KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &key, NULL);
    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    if (rc != ERROR_SUCCESS) {
        return WT_ERR_WIN32;
    }

    BYTE blob[12] = {0};
    blob[0] = enabled ? 0x02 : 0x03;
    rc = RegSetValueExW(key, value_name, 0, REG_BINARY, blob, sizeof(blob));
    RegCloseKey(key);

    if (rc == ERROR_ACCESS_DENIED) {
        return WT_ERR_ACCESS_DENIED;
    }
    return (rc == ERROR_SUCCESS) ? WT_OK : WT_ERR_WIN32;
}

/* Maps a startup entry to its StartupApproved location. Returns WT_OK and fills
 * hive_tag/subkey for supported sources, WT_ERR_NOT_SUPPORTED otherwise. */
static WT_Result wt_startup_approved_location(WT_StartupSource src,
                                              const wchar_t **hive_tag,
                                              const wchar_t **subkey)
{
    switch (src) {
    case WT_STARTUP_SRC_HKCU_RUN:
        *hive_tag = L"HKCU"; *subkey = WT_APPROVED_RUN; return WT_OK;
    case WT_STARTUP_SRC_HKLM_RUN:
        *hive_tag = L"HKLM"; *subkey = WT_APPROVED_RUN; return WT_OK;
    case WT_STARTUP_SRC_USER_FOLDER:
        *hive_tag = L"HKCU"; *subkey = WT_APPROVED_FOLDER; return WT_OK;
    case WT_STARTUP_SRC_COMMON_FOLDER:
        *hive_tag = L"HKLM"; *subkey = WT_APPROVED_FOLDER; return WT_OK;
    default:
        return WT_ERR_NOT_SUPPORTED;
    }
}

static int wt_startup_read_enabled(const wchar_t *hive_tag,
                                   const wchar_t *subkey,
                                   const wchar_t *value_name)
{
    HKEY root = wt_hive_from_tag(hive_tag);
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return 1; /* no approved key yet => entry is enabled */
    }
    BYTE blob[12] = {0};
    DWORD size = sizeof(blob);
    DWORD type = 0;
    int enabled = 1;
    if (RegQueryValueExW(key, value_name, NULL, &type, blob, &size) == ERROR_SUCCESS
            && size >= 1) {
        enabled = ((blob[0] & 1) == 0); /* even => enabled, odd => disabled */
    }
    RegCloseKey(key);
    return enabled;
}

WT_Result wt_action_set_startup_enabled(const wchar_t *id,
                                        int enable,
                                        int assume_yes,
                                        char *msg, size_t msg_cap)
{
    if (msg != NULL && msg_cap > 0) {
        msg[0] = '\0';
    }
    if (id == NULL || id[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    WT_StartupEntry *entries =
        (WT_StartupEntry *)malloc(sizeof(WT_StartupEntry) * WT_MAX_STARTUP_ENTRIES);
    if (entries == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t count = 0;
    if (wt_collect_startup_entries(entries, WT_MAX_STARTUP_ENTRIES, &count) != WT_OK) {
        free(entries);
        StringCchPrintfA(msg, msg_cap, "Could not read startup entries.");
        return WT_ERR_WIN32;
    }

    const WT_StartupEntry *entry = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (_wcsicmp(entries[i].id, id) == 0) {
            entry = &entries[i];
            break;
        }
    }
    if (entry == NULL) {
        StringCchPrintfA(msg, msg_cap,
                         "No startup entry with id '%ls'. "
                         "List ids with 'wintune startup'.", id);
        free(entries);
        return WT_ERR_NOT_FOUND;
    }

    const wchar_t *hive_tag = NULL;
    const wchar_t *subkey = NULL;
    WT_Result loc = wt_startup_approved_location(entry->source, &hive_tag, &subkey);
    if (loc != WT_OK) {
        StringCchPrintfA(msg, msg_cap,
                         "This startup entry's source is not supported for "
                         "enable/disable in v1.");
        free(entries);
        return loc;
    }

    wchar_t value_name[256];
    StringCchCopyW(value_name, ARRAYSIZE(value_name), entry->name);

    if (_wcsicmp(hive_tag, L"HKLM") == 0 && !wt_is_process_elevated()) {
        wt_print_admin_required_message(stderr);
        StringCchPrintfA(msg, msg_cap,
                         "Changing this machine-wide startup entry requires "
                         "administrator privileges.");
        free(entries);
        return WT_ERR_ACCESS_DENIED;
    }

    int prev_enabled = wt_startup_read_enabled(hive_tag, subkey, value_name);
    if ((enable && prev_enabled) || (!enable && !prev_enabled)) {
        StringCchPrintfA(msg, msg_cap, "Startup entry '%ls' is already %s.",
                         entry->name, enable ? "enabled" : "disabled");
        free(entries);
        return WT_OK;
    }

    char prompt[320];
    StringCchPrintfA(prompt, sizeof(prompt), "%s startup entry '%ls'?",
                     enable ? "Enable" : "Disable", entry->name);
    if (!wt_action_confirm(prompt, assume_yes)) {
        StringCchPrintfA(msg, msg_cap, "Cancelled. Startup entry unchanged.");
        free(entries);
        return WT_ERR_CANCELLED;
    }

    WT_Result wr = wt_startup_write_approved(hive_tag, subkey, value_name, enable);
    if (wr != WT_OK) {
        StringCchPrintfA(msg, msg_cap, "Failed to update startup entry (%s).",
                         wt_result_to_string(wr));
        free(entries);
        return wr;
    }

    WT_RollbackRecord rec;
    ZeroMemory(&rec, sizeof(rec));
    StringCchCopyW(rec.action_type, ARRAYSIZE(rec.action_type), L"startup_approved");
    StringCchCopyW(rec.action_id, ARRAYSIZE(rec.action_id), id);
    StringCchPrintfW(rec.description, ARRAYSIZE(rec.description),
                     L"Startup '%s': %s -> %s", entry->name,
                     prev_enabled ? L"enabled" : L"disabled",
                     enable ? L"enabled" : L"disabled");
    /* previous_value encodes how to restore: hive|subkey|name|prevbyte */
    StringCchPrintfW(rec.previous_value, ARRAYSIZE(rec.previous_value),
                     L"%s|%s|%s|%s", hive_tag, subkey, value_name,
                     prev_enabled ? L"02" : L"03");
    StringCchPrintfW(rec.new_value, ARRAYSIZE(rec.new_value),
                     L"%s|%s|%s|%s", hive_tag, subkey, value_name,
                     enable ? L"02" : L"03");

    WT_Result rr = wt_rollback_write(&rec);
    if (rr == WT_OK) {
        StringCchPrintfA(msg, msg_cap, "Startup entry '%ls' %s. Rollback id: %ls",
                         entry->name, enable ? "enabled" : "disabled", rec.id);
    } else {
        WT_LOGW("could not write rollback record (%s)", wt_result_to_string(rr));
        StringCchPrintfA(msg, msg_cap, "Startup entry '%ls' %s. (rollback not saved)",
                         entry->name, enable ? "enabled" : "disabled");
    }

    free(entries);
    return WT_OK;
}
