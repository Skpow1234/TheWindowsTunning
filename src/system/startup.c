#include "system/startup.h"
#include "system/boot.h"
#include "system/file_identity.h"
#include "core/impact_score.h"

#include <windows.h>
#include <strsafe.h>

const char *wt_startup_source_name(WT_StartupSource source)
{
    switch (source) {
    case WT_STARTUP_SRC_HKCU_RUN:      return "HKCU\\Run";
    case WT_STARTUP_SRC_HKLM_RUN:      return "HKLM\\Run";
    case WT_STARTUP_SRC_HKCU_RUNONCE:  return "HKCU\\RunOnce";
    case WT_STARTUP_SRC_HKLM_RUNONCE:  return "HKLM\\RunOnce";
    case WT_STARTUP_SRC_USER_FOLDER:   return "Startup folder (user)";
    case WT_STARTUP_SRC_COMMON_FOLDER: return "Startup folder (common)";
    default:                           return "Unknown";
    }
}

const char *wt_startup_impact_name(WT_StartupImpact impact)
{
    switch (impact) {
    case WT_STARTUP_IMPACT_LOW:    return "low";
    case WT_STARTUP_IMPACT_MEDIUM: return "medium";
    case WT_STARTUP_IMPACT_HIGH:   return "high";
    default:                       return "unknown";
    }
}

/* Evidence-based impact (Phase 24). Measured boot data refreshes the score. */
static void wt_startup_rescore(WT_StartupEntry *e)
{
    WT_ImpactInput in;
    WT_ImpactScore score;
    wt_impact_input_from_startup(e, &in);
    wt_impact_score_compute(&in, &score);
    wt_impact_apply_to_startup(e, &score);
}

static void wt_startup_add(WT_StartupEntry *out, size_t capacity, size_t *count,
                           WT_StartupSource source, const wchar_t *name,
                           const wchar_t *command)
{
    if (*count >= capacity) {
        return;
    }
    WT_StartupEntry *e = &out[*count];
    ZeroMemory(e, sizeof(*e));
    e->source = source;
    e->enabled = 1;
    StringCchCopyW(e->name, ARRAYSIZE(e->name), name ? name : L"");
    StringCchCopyW(e->command, ARRAYSIZE(e->command), command ? command : L"");
    StringCchPrintfW(e->id, ARRAYSIZE(e->id), L"%S:%s",
                     wt_startup_source_name(source), e->name);
    if (wt_identity_from_command(e->command, &e->identity) != WT_OK) {
        ZeroMemory(&e->identity, sizeof(e->identity));
        e->identity.signature = WT_SIG_UNAVAILABLE;
        e->identity.origin = WT_ORIGIN_UNKNOWN;
    }
    wt_startup_rescore(e);
    (*count)++;
}

static void wt_scan_run_key(HKEY root, const wchar_t *subkey,
                            WT_StartupSource source, WT_StartupEntry *out,
                            size_t capacity, size_t *count)
{
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    for (;;) {
        wchar_t name[256];
        DWORD name_len = ARRAYSIZE(name);
        BYTE data[1024];
        DWORD data_len = sizeof(data);
        DWORD type = 0;

        LONG rc = RegEnumValueW(key, index, name, &name_len, NULL, &type,
                                data, &data_len);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        index++;
        if (rc != ERROR_SUCCESS) {
            continue;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ) {
            continue;
        }

        const wchar_t *raw = (const wchar_t *)data;
        wchar_t expanded[512];
        if (type == REG_EXPAND_SZ) {
            if (ExpandEnvironmentStringsW(raw, expanded, ARRAYSIZE(expanded)) > 0) {
                raw = expanded;
            }
        }
        wt_startup_add(out, capacity, count, source, name, raw);
    }

    RegCloseKey(key);
}

static void wt_scan_startup_folder(const wchar_t *env_var, const wchar_t *suffix,
                                   WT_StartupSource source, WT_StartupEntry *out,
                                   size_t capacity, size_t *count)
{
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(env_var, base, ARRAYSIZE(base));
    if (n == 0 || n >= ARRAYSIZE(base)) {
        return;
    }

    wchar_t folder[MAX_PATH];
    if (FAILED(StringCchPrintfW(folder, ARRAYSIZE(folder), L"%s%s", base, suffix))) {
        return;
    }

    wchar_t pattern[MAX_PATH];
    if (FAILED(StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\*", folder))) {
        return;
    }

    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (wcscmp(fd.cFileName, L"desktop.ini") == 0) {
            continue;
        }
        wchar_t full[MAX_PATH];
        if (FAILED(StringCchPrintfW(full, ARRAYSIZE(full), L"%s\\%s",
                                    folder, fd.cFileName))) {
            continue;
        }
        wt_startup_add(out, capacity, count, source, fd.cFileName, full);
    } while (FindNextFileW(find, &fd));

    FindClose(find);
}

WT_Result wt_collect_startup_entries(WT_StartupEntry *out,
                                     size_t capacity,
                                     size_t *out_count)
{
    if (out == NULL || out_count == NULL || capacity == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    static const wchar_t *RUN =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static const wchar_t *RUNONCE =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    static const wchar_t *FOLDER_SUFFIX =
        L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";

    wt_scan_run_key(HKEY_CURRENT_USER, RUN, WT_STARTUP_SRC_HKCU_RUN,
                    out, capacity, out_count);
    wt_scan_run_key(HKEY_LOCAL_MACHINE, RUN, WT_STARTUP_SRC_HKLM_RUN,
                    out, capacity, out_count);
    wt_scan_run_key(HKEY_CURRENT_USER, RUNONCE, WT_STARTUP_SRC_HKCU_RUNONCE,
                    out, capacity, out_count);
    wt_scan_run_key(HKEY_LOCAL_MACHINE, RUNONCE, WT_STARTUP_SRC_HKLM_RUNONCE,
                    out, capacity, out_count);

    wt_scan_startup_folder(L"APPDATA", FOLDER_SUFFIX,
                           WT_STARTUP_SRC_USER_FOLDER, out, capacity, out_count);
    wt_scan_startup_folder(L"PROGRAMDATA", FOLDER_SUFFIX,
                           WT_STARTUP_SRC_COMMON_FOLDER, out, capacity, out_count);

    return WT_OK;
}

void wt_startup_apply_measured(WT_StartupEntry *entries, size_t count,
                               const WT_BootReport *boot)
{
    if (entries == NULL || boot == NULL) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        WT_StartupEntry *e = &entries[i];
        e->measured_ms = 0;
        e->measured_available = 0;

        unsigned long ms = 0;
        int matched = 0;
        wt_boot_apply_measured_startup(boot, e->name, e->command, &ms, &matched);
        if (!matched) {
            continue;
        }

        e->measured_ms = ms;
        e->measured_available = 1;
        wt_startup_rescore(e);
    }
}
