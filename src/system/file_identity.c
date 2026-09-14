#include "system/file_identity.h"

#include <strsafe.h>
#include <wintrust.h>
#include <softpub.h>

#include <stdlib.h>
#include <string.h>

/* Small process-lifetime cache so services/top do not re-verify the same path. */
#define WT_IDENTITY_CACHE_MAX 256

typedef struct WT_IdentityCacheEntry {
    wchar_t path[MAX_PATH];
    WT_FileIdentity identity;
    int used;
} WT_IdentityCacheEntry;

static WT_IdentityCacheEntry g_identity_cache[WT_IDENTITY_CACHE_MAX];
static size_t g_identity_cache_count = 0;

const char *wt_signature_status_name(WT_SignatureStatus status)
{
    switch (status) {
    case WT_SIG_UNSIGNED:          return "unsigned";
    case WT_SIG_SIGNED:            return "signed";
    case WT_SIG_SIGNED_MICROSOFT:  return "signed-microsoft";
    case WT_SIG_UNAVAILABLE:       return "unavailable";
    case WT_SIG_UNKNOWN:
    default:                       return "unknown";
    }
}

const char *wt_publisher_origin_name(WT_PublisherOrigin origin)
{
    switch (origin) {
    case WT_ORIGIN_MICROSOFT:    return "microsoft";
    case WT_ORIGIN_THIRD_PARTY:  return "third-party";
    case WT_ORIGIN_UNKNOWN:
    default:                     return "unknown";
    }
}

static int wt_wcs_ieq_prefix(const wchar_t *s, const wchar_t *prefix)
{
    size_t n = wcslen(prefix);
    return _wcsnicmp(s, prefix, n) == 0;
}

static int wt_publisher_looks_microsoft(const wchar_t *publisher)
{
    if (publisher == NULL || publisher[0] == L'\0') {
        return 0;
    }
    /* Calm substring match — version CompanyName and catalog subjects vary. */
    if (wcsstr(publisher, L"Microsoft") != NULL) {
        return 1;
    }
    if (_wcsicmp(publisher, L"Microsoft Corporation") == 0) {
        return 1;
    }
    return 0;
}

static void wt_identity_clear(WT_FileIdentity *out)
{
    memset(out, 0, sizeof(*out));
    out->signature = WT_SIG_UNAVAILABLE;
    out->origin = WT_ORIGIN_UNKNOWN;
}

static int wt_cache_lookup(const wchar_t *path, WT_FileIdentity *out)
{
    for (size_t i = 0; i < g_identity_cache_count; ++i) {
        if (g_identity_cache[i].used &&
            _wcsicmp(g_identity_cache[i].path, path) == 0) {
            *out = g_identity_cache[i].identity;
            return 1;
        }
    }
    return 0;
}

static void wt_cache_store(const wchar_t *path, const WT_FileIdentity *id)
{
    if (g_identity_cache_count >= WT_IDENTITY_CACHE_MAX) {
        return;
    }
    WT_IdentityCacheEntry *e = &g_identity_cache[g_identity_cache_count++];
    e->used = 1;
    StringCchCopyW(e->path, MAX_PATH, path);
    e->identity = *id;
}

WT_Result wt_identity_extract_path(const wchar_t *command_or_path,
                                   wchar_t *out,
                                   size_t out_count)
{
    if (command_or_path == NULL || out == NULL || out_count == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    out[0] = L'\0';

    const wchar_t *p = command_or_path;
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    if (*p == L'\0') {
        return WT_ERR_NOT_FOUND;
    }

    wchar_t buf[MAX_PATH];
    size_t n = 0;

    if (*p == L'"') {
        ++p;
        while (*p != L'\0' && *p != L'"' && n + 1 < MAX_PATH) {
            buf[n++] = *p++;
        }
    } else {
        /* Unquoted: take until whitespace, but keep drive paths like C:\... */
        while (*p != L'\0' && *p != L' ' && *p != L'\t' && n + 1 < MAX_PATH) {
            buf[n++] = *p++;
        }
    }
    buf[n] = L'\0';
    if (n == 0) {
        return WT_ERR_NOT_FOUND;
    }

    /* Expand %ENV% if present. */
    wchar_t expanded[MAX_PATH];
    DWORD exp = ExpandEnvironmentStringsW(buf, expanded, MAX_PATH);
    const wchar_t *src = (exp > 0 && exp < MAX_PATH) ? expanded : buf;

    if (FAILED(StringCchCopyW(out, out_count, src))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return WT_OK;
}

static WT_Result wt_read_company_name(const wchar_t *path, wchar_t *out, size_t out_count)
{
    out[0] = L'\0';
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (size == 0) {
        return WT_ERR_NOT_FOUND;
    }

    BYTE *block = (BYTE *)malloc(size);
    if (block == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    WT_Result result = WT_ERR_NOT_FOUND;
    if (GetFileVersionInfoW(path, 0, size, block)) {
        struct LANGANDCODEPAGE {
            WORD language;
            WORD codepage;
        } *translate = NULL;
        UINT translate_len = 0;
        if (VerQueryValueW(block, L"\\VarFileInfo\\Translation",
                           (LPVOID *)&translate, &translate_len) &&
            translate != NULL && translate_len >= sizeof(*translate)) {
            wchar_t sub[64];
            StringCchPrintfW(sub, 64, L"\\StringFileInfo\\%04x%04x\\CompanyName",
                             translate[0].language, translate[0].codepage);
            wchar_t *company = NULL;
            UINT company_len = 0;
            if (VerQueryValueW(block, sub, (LPVOID *)&company, &company_len) &&
                company != NULL && company_len > 0) {
                StringCchCopyW(out, out_count, company);
                result = WT_OK;
            }
        }
    }
    free(block);
    return result;
}

static WT_SignatureStatus wt_verify_authenticode(const wchar_t *path,
                                                 wchar_t *signer_out,
                                                 size_t signer_count)
{
    if (signer_out != NULL && signer_count > 0) {
        signer_out[0] = L'\0';
    }

    WINTRUST_FILE_INFO file_info;
    memset(&file_info, 0, sizeof(file_info));
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = path;
    file_info.hFile = NULL;
    file_info.pgKnownSubject = NULL;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data;
    memset(&data, 0, sizeof(data));
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE; /* offline-friendly */
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file_info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    LONG status = WinVerifyTrust(NULL, &action, &data);

    /* Always close the state regardless of result. */
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &action, &data);

    if (status == ERROR_SUCCESS) {
        return WT_SIG_SIGNED;
    }
    if (status == TRUST_E_NOSIGNATURE) {
        return WT_SIG_UNSIGNED;
    }
    /* Other trust failures (revoked, bad cert, etc.) — report unavailable,
     * not "unsigned", to avoid alarming false cues. */
    (void)signer_out;
    (void)signer_count;
    return WT_SIG_UNAVAILABLE;
}

WT_Result wt_identity_from_path(const wchar_t *path, WT_FileIdentity *out)
{
    if (path == NULL || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_identity_clear(out);

    if (path[0] == L'\0') {
        return WT_ERR_NOT_FOUND;
    }

    if (wt_cache_lookup(path, out)) {
        return WT_OK;
    }

    StringCchCopyW(out->path, MAX_PATH, path);

    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        out->signature = WT_SIG_UNAVAILABLE;
        out->available = 0;
        wt_cache_store(path, out);
        return WT_OK;
    }

    out->available = 1;
    (void)wt_read_company_name(path, out->publisher, 128);

    WT_SignatureStatus sig = wt_verify_authenticode(path, NULL, 0);
    out->signature = sig;

    if (wt_publisher_looks_microsoft(out->publisher)) {
        out->origin = WT_ORIGIN_MICROSOFT;
        if (sig == WT_SIG_SIGNED) {
            out->signature = WT_SIG_SIGNED_MICROSOFT;
        }
    } else if (out->publisher[0] != L'\0') {
        out->origin = WT_ORIGIN_THIRD_PARTY;
    } else if (sig == WT_SIG_SIGNED) {
        /* Signed but no CompanyName — still third-party cue, not "unknown malware". */
        out->origin = WT_ORIGIN_THIRD_PARTY;
    } else {
        out->origin = WT_ORIGIN_UNKNOWN;
    }

    /* Heuristic: binaries under Windows\System32 / WinSxS without version info
     * are usually Microsoft components. */
    if (out->origin == WT_ORIGIN_UNKNOWN) {
        if (wt_wcs_ieq_prefix(path, L"C:\\Windows\\System32\\") ||
            wt_wcs_ieq_prefix(path, L"C:\\Windows\\SysWOW64\\") ||
            wcsstr(path, L"\\Windows\\System32\\") != NULL ||
            wcsstr(path, L"\\Windows\\SysWOW64\\") != NULL) {
            out->origin = WT_ORIGIN_MICROSOFT;
            if (sig == WT_SIG_SIGNED) {
                out->signature = WT_SIG_SIGNED_MICROSOFT;
            }
        }
    }

    wt_cache_store(path, out);
    return WT_OK;
}

WT_Result wt_identity_from_command(const wchar_t *command, WT_FileIdentity *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_identity_clear(out);

    wchar_t path[MAX_PATH];
    WT_Result r = wt_identity_extract_path(command, path, MAX_PATH);
    if (r != WT_OK) {
        return r;
    }
    return wt_identity_from_path(path, out);
}

WT_Result wt_identity_from_pid(unsigned long pid, WT_FileIdentity *out)
{
    if (out == NULL || pid == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_identity_clear(out);

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == NULL) {
        out->signature = WT_SIG_UNAVAILABLE;
        return WT_ERR_ACCESS_DENIED;
    }

    wchar_t path[MAX_PATH];
    DWORD n = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(h, 0, path, &n);
    CloseHandle(h);
    if (!ok) {
        out->signature = WT_SIG_UNAVAILABLE;
        return WT_ERR_WIN32;
    }
    return wt_identity_from_path(path, out);
}
