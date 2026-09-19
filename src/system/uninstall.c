#include "system/uninstall.h"

#include "system/startup.h"
#include "core/impact_score.h"

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int wt_uninstall_is_microsoft_publisher(const wchar_t *publisher)
{
    if (publisher == NULL || publisher[0] == L'\0') {
        return 0;
    }
    return (_wcsnicmp(publisher, L"Microsoft", 9) == 0) ? 1 : 0;
}

static int wt_uninstall_should_skip_name(const wchar_t *name)
{
    if (name == NULL || name[0] == L'\0') {
        return 1;
    }
    /* Hotfix / Update rollups are not useful uninstall targets. */
    if (_wcsnicmp(name, L"Update for", 10) == 0 ||
        _wcsnicmp(name, L"Security Update", 15) == 0 ||
        _wcsnicmp(name, L"Hotfix", 6) == 0 ||
        wcsstr(name, L"KB") != NULL) {
        return 1;
    }
    return 0;
}

static void wt_reg_read_sz(HKEY key, const wchar_t *value, wchar_t *out,
                           size_t count)
{
    if (out == NULL || count == 0) {
        return;
    }
    out[0] = L'\0';
    DWORD type = 0;
    DWORD size = (DWORD)(count * sizeof(wchar_t));
    if (RegQueryValueExW(key, value, NULL, &type, (LPBYTE)out, &size) !=
            ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        out[0] = L'\0';
        return;
    }
    out[count - 1] = L'\0';
}

static unsigned long wt_reg_read_dword(HKEY key, const wchar_t *value)
{
    DWORD type = 0;
    DWORD data = 0;
    DWORD size = sizeof(data);
    if (RegQueryValueExW(key, value, NULL, &type, (LPBYTE)&data, &size) !=
            ERROR_SUCCESS ||
        type != REG_DWORD) {
        return 0;
    }
    return (unsigned long)data;
}

static int wt_wide_contains_ci(const wchar_t *hay, const wchar_t *needle)
{
    if (hay == NULL || needle == NULL || needle[0] == L'\0') {
        return 0;
    }
    size_t nlen = wcslen(needle);
    if (nlen == 0) {
        return 0;
    }
    for (const wchar_t *p = hay; *p != L'\0'; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

static int wt_app_matches_startup(const WT_InstalledApp *app,
                                  const WT_StartupEntry *e)
{
    if (app == NULL || e == NULL) {
        return 0;
    }
    if (app->display_name[0] != L'\0' &&
        (wt_wide_contains_ci(e->name, app->display_name) ||
         wt_wide_contains_ci(app->display_name, e->name) ||
         wt_wide_contains_ci(e->command, app->display_name))) {
        return 1;
    }
    if (app->install_location[0] != L'\0' &&
        wt_wide_contains_ci(e->command, app->install_location)) {
        return 1;
    }
    if (app->publisher[0] != L'\0' && e->identity.publisher[0] != L'\0' &&
        _wcsicmp(app->publisher, e->identity.publisher) == 0 &&
        (wt_wide_contains_ci(e->command, app->display_name) ||
         wt_wide_contains_ci(e->name, app->display_name))) {
        return 1;
    }
    return 0;
}

static void wt_uninstall_flag_candidates(WT_UninstallAdvice *out,
                                         const WT_StartupEntry *entries,
                                         size_t entry_count,
                                         int correlate_startup,
                                         int include_large)
{
    for (size_t i = 0; i < out->count; ++i) {
        WT_InstalledApp *a = &out->apps[i];
        a->candidate = 0;
        a->startup_related = 0;
        a->reason[0] = '\0';

        if (a->system_component || a->is_microsoft) {
            continue;
        }
        if (a->display_name[0] == L'\0') {
            continue;
        }

        if (correlate_startup && entries != NULL) {
            for (size_t s = 0; s < entry_count; ++s) {
                const WT_StartupEntry *e = &entries[s];
                if (!e->enabled) {
                    continue;
                }
                if (e->impact_score < WT_IMPACT_RECOMMEND_SCORE_MIN) {
                    continue;
                }
                if (e->identity.origin == WT_ORIGIN_MICROSOFT) {
                    continue;
                }
                if (!wt_app_matches_startup(a, e)) {
                    continue;
                }
                a->startup_related = 1;
                a->candidate = 1;
                StringCchPrintfA(a->reason, sizeof(a->reason),
                                 "Matches high-impact startup '%ls' "
                                 "(score %d). Review official uninstall path.",
                                 e->name, e->impact_score);
                break;
            }
        }

        if (!a->candidate && include_large &&
            a->estimated_kb >= (2048ul * 1024ul)) {
            a->candidate = 1;
            StringCchPrintfA(a->reason, sizeof(a->reason),
                             "Large third-party install (~%lu MB estimated). "
                             "Only remove if you no longer need it.",
                             a->estimated_kb / 1024ul);
        }
    }

    out->candidate_count = 0;
    for (size_t i = 0; i < out->count; ++i) {
        if (out->apps[i].candidate) {
            out->candidate_count++;
        }
    }
}

static WT_Result wt_uninstall_enum_key(HKEY root, const wchar_t *subpath,
                                       WT_UninstallAdvice *out)
{
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subpath, 0, KEY_READ | KEY_WOW64_64KEY, &key) !=
        ERROR_SUCCESS) {
        /* Try without WOW64 flag for HKCU / older layouts. */
        if (RegOpenKeyExW(root, subpath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return WT_OK; /* partial OK */
        }
    }

    wchar_t name[256];
    DWORD index = 0;
    for (;;) {
        DWORD name_len = ARRAYSIZE(name);
        LONG rc = RegEnumKeyExW(key, index++, name, &name_len, NULL, NULL, NULL,
                                NULL);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            continue;
        }
        out->scanned_keys++;

        if (out->count >= WT_MAX_INSTALLED_APPS) {
            break;
        }

        HKEY sub = NULL;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &sub) != ERROR_SUCCESS) {
            continue;
        }

        WT_InstalledApp *a = &out->apps[out->count];
        ZeroMemory(a, sizeof(*a));
        StringCchCopyW(a->key_id, ARRAYSIZE(a->key_id), name);
        wt_reg_read_sz(sub, L"DisplayName", a->display_name,
                       ARRAYSIZE(a->display_name));
        wt_reg_read_sz(sub, L"Publisher", a->publisher, ARRAYSIZE(a->publisher));
        wt_reg_read_sz(sub, L"DisplayVersion", a->version,
                       ARRAYSIZE(a->version));
        wt_reg_read_sz(sub, L"InstallLocation", a->install_location,
                       ARRAYSIZE(a->install_location));
        wt_reg_read_sz(sub, L"UninstallString", a->uninstall_string,
                       ARRAYSIZE(a->uninstall_string));
        a->estimated_kb = wt_reg_read_dword(sub, L"EstimatedSize");
        a->system_component = (wt_reg_read_dword(sub, L"SystemComponent") != 0)
                                  ? 1
                                  : 0;
        a->is_microsoft = wt_uninstall_is_microsoft_publisher(a->publisher);
        RegCloseKey(sub);

        if (a->system_component || wt_uninstall_should_skip_name(a->display_name)) {
            continue;
        }
        if (a->display_name[0] == L'\0') {
            continue;
        }
        out->count++;
    }

    RegCloseKey(key);
    return WT_OK;
}

WT_Result wt_collect_uninstall_advice(WT_UninstallAdvice *out,
                                      int correlate_startup,
                                      int include_large)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));

    static const struct {
        HKEY root;
        const wchar_t *path;
    } sources[] = {
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
        { HKEY_LOCAL_MACHINE,
          L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
        { HKEY_CURRENT_USER,
          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
    };

    for (size_t i = 0; i < ARRAYSIZE(sources); ++i) {
        (void)wt_uninstall_enum_key(sources[i].root, sources[i].path, out);
    }

    WT_StartupEntry *entries = NULL;
    size_t entry_count = 0;
    if (correlate_startup) {
        entries = (WT_StartupEntry *)malloc(sizeof(WT_StartupEntry) *
                                            WT_MAX_STARTUP_ENTRIES);
        if (entries != NULL) {
            if (wt_collect_startup_entries(entries, WT_MAX_STARTUP_ENTRIES,
                                           &entry_count) == WT_OK) {
                for (size_t i = 0; i < entry_count; ++i) {
                    WT_ImpactInput in;
                    WT_ImpactScore score;
                    wt_impact_input_from_startup(&entries[i], &in);
                    wt_impact_score_compute(&in, &score);
                    wt_impact_apply_to_startup(&entries[i], &score);
                }
            } else {
                entry_count = 0;
            }
        }
    }

    wt_uninstall_flag_candidates(out, entries, entry_count, correlate_startup,
                                 include_large);

    if (entries != NULL) {
        free(entries);
    }
    return WT_OK;
}
