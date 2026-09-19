#include "fleet/fleet_pack.h"

#include "common/sha256.h"
#include "common/zip_store.h"
#include "output/json.h"
#include "platform/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef WT_VERSION_STRING
#define WT_VERSION_STRING "0.1.0"
#endif

#define WT_FLEET_PACK_MAX_FILES 512
#define WT_FLEET_PACK_MAX_BYTES (64ull * 1024ull * 1024ull)

typedef struct WT_FleetFile {
    wchar_t full_path[MAX_PATH];
    char zip_name[280]; /* reports/<basename> UTF-8 */
    char base_utf8[260];
    unsigned long long size;
    unsigned long crc32;
    unsigned char sha256[32];
} WT_FleetFile;

static int wt_fleet_is_skipped_name(const wchar_t *name)
{
    if (name == NULL) {
        return 1;
    }
    if (_wcsicmp(name, L"manifest.json") == 0) {
        return 1;
    }
    if (_wcsicmp(name, L"CHECKSUMS.sha256") == 0) {
        return 1;
    }
    if (_wcsicmp(name, L"SHA256SUMS") == 0) {
        return 1;
    }
    return 0;
}

static int wt_fleet_has_json_ext(const wchar_t *name)
{
    size_t n;
    if (name == NULL) {
        return 0;
    }
    n = wcslen(name);
    if (n < 5) {
        return 0;
    }
    return _wcsicmp(name + n - 5, L".json") == 0;
}

static WT_Result wt_fleet_wide_to_utf8(const wchar_t *ws, char *out, size_t out_cap)
{
    int n;
    if (ws == NULL || out == NULL || out_cap == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, (int)out_cap, NULL, NULL);
    if (n <= 0) {
        return WT_ERR_WIN32;
    }
    return WT_OK;
}

static WT_Result wt_fleet_hash_and_crc_file(WT_FleetFile *f)
{
    HANDLE h;
    unsigned char buf[64 * 1024];
    unsigned long crc = 0xffffffffu;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS st;
    DWORD obj_len = 0;
    DWORD cb = 0;
    unsigned char *obj = NULL;
    unsigned char dig[32];
    WT_Result rc = WT_OK;
    unsigned long long total = 0;

    if (f == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    h = CreateFileW(f->full_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return WT_ERR_NOT_FOUND;
        }
        return WT_ERR_WIN32;
    }

    st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(st)) {
        rc = WT_ERR_WIN32;
        goto done;
    }
    st = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len,
                           sizeof(obj_len), &cb, 0);
    if (!NT_SUCCESS(st) || obj_len == 0) {
        rc = WT_ERR_WIN32;
        goto done;
    }
    obj = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, obj_len);
    if (obj == NULL) {
        rc = WT_ERR_OUT_OF_MEMORY;
        goto done;
    }
    st = BCryptCreateHash(alg, &hash, obj, obj_len, NULL, 0, 0);
    if (!NT_SUCCESS(st)) {
        rc = WT_ERR_WIN32;
        goto done;
    }

    for (;;) {
        DWORD n = 0;
        if (!ReadFile(h, buf, (DWORD)sizeof(buf), &n, NULL)) {
            rc = WT_ERR_WIN32;
            goto done;
        }
        if (n == 0) {
            break;
        }
        total += n;
        if (total > WT_FLEET_PACK_MAX_BYTES) {
            rc = WT_ERR_NOT_SUPPORTED;
            goto done;
        }
        crc = wt_crc32_update(crc, buf, n);
        st = BCryptHashData(hash, buf, n, 0);
        if (!NT_SUCCESS(st)) {
            rc = WT_ERR_WIN32;
            goto done;
        }
    }

    st = BCryptFinishHash(hash, dig, 32, 0);
    if (!NT_SUCCESS(st)) {
        rc = WT_ERR_WIN32;
        goto done;
    }

    f->size = total;
    f->crc32 = crc ^ 0xffffffffu;
    memcpy(f->sha256, dig, 32);

done:
    if (hash != NULL) {
        BCryptDestroyHash(hash);
    }
    if (obj != NULL) {
        HeapFree(GetProcessHeap(), 0, obj);
    }
    if (alg != NULL) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(h);
    return rc;
}

static int wt_fleet_file_cmp(const void *a, const void *b)
{
    const WT_FleetFile *fa = (const WT_FleetFile *)a;
    const WT_FleetFile *fb = (const WT_FleetFile *)b;
    return _stricmp(fa->base_utf8, fb->base_utf8);
}

static WT_Result wt_fleet_collect(const wchar_t *input_dir, WT_FleetFile **out_files,
                                  size_t *out_count)
{
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find = INVALID_HANDLE_VALUE;
    WT_FleetFile *files = NULL;
    size_t count = 0;
    size_t cap = 0;
    HRESULT hr;

    if (input_dir == NULL || out_files == NULL || out_count == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_files = NULL;
    *out_count = 0;

    hr = StringCchPrintfW(pattern, MAX_PATH, L"%ls\\*", input_dir);
    if (FAILED(hr)) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return WT_OK;
        }
        if (err == ERROR_PATH_NOT_FOUND) {
            return WT_ERR_NOT_FOUND;
        }
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        return WT_ERR_WIN32;
    }

    do {
        WT_FleetFile *slot;
        WT_Result r;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!wt_fleet_has_json_ext(fd.cFileName)) {
            continue;
        }
        if (wt_fleet_is_skipped_name(fd.cFileName)) {
            continue;
        }
        if (count >= WT_FLEET_PACK_MAX_FILES) {
            FindClose(find);
            free(files);
            return WT_ERR_NOT_SUPPORTED;
        }

        if (count == cap) {
            size_t ncap = (cap == 0) ? 16u : cap * 2u;
            WT_FleetFile *nf =
                (WT_FleetFile *)realloc(files, ncap * sizeof(WT_FleetFile));
            if (nf == NULL) {
                FindClose(find);
                free(files);
                return WT_ERR_OUT_OF_MEMORY;
            }
            files = nf;
            cap = ncap;
        }

        slot = &files[count];
        memset(slot, 0, sizeof(*slot));
        hr = StringCchPrintfW(slot->full_path, MAX_PATH, L"%ls\\%ls", input_dir,
                              fd.cFileName);
        if (FAILED(hr)) {
            FindClose(find);
            free(files);
            return WT_ERR_INVALID_ARGUMENT;
        }

        r = wt_fleet_wide_to_utf8(fd.cFileName, slot->base_utf8,
                                  sizeof(slot->base_utf8));
        if (r != WT_OK) {
            FindClose(find);
            free(files);
            return r;
        }
        hr = StringCchPrintfA(slot->zip_name, sizeof(slot->zip_name),
                              "reports/%s", slot->base_utf8);
        if (FAILED(hr)) {
            FindClose(find);
            free(files);
            return WT_ERR_INVALID_ARGUMENT;
        }

        r = wt_fleet_hash_and_crc_file(slot);
        if (r != WT_OK) {
            FindClose(find);
            free(files);
            return r;
        }
        if (slot->size > 0xffffffffu) {
            FindClose(find);
            free(files);
            return WT_ERR_NOT_SUPPORTED;
        }
        count++;
    } while (FindNextFileW(find, &fd));

    FindClose(find);

    if (count > 1) {
        qsort(files, count, sizeof(WT_FleetFile), wt_fleet_file_cmp);
    }

    *out_files = files;
    *out_count = count;
    return WT_OK;
}

static char *wt_fleet_build_checksums(const WT_FleetFile *files, size_t count,
                                      size_t *out_len)
{
    size_t cap = count * 96u + 64u;
    size_t len = 0;
    char *buf;
    size_t i;

    if (out_len == NULL) {
        return NULL;
    }
    *out_len = 0;
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    buf[0] = '\0';

    for (i = 0; i < count; ++i) {
        char hex[65];
        char line[360];
        size_t line_len;
        HRESULT hr;

        wt_sha256_hex(files[i].sha256, hex);
        hr = StringCchPrintfA(line, sizeof(line), "%s  %s\n", hex,
                              files[i].zip_name);
        if (FAILED(hr)) {
            free(buf);
            return NULL;
        }
        line_len = strlen(line);
        if (len + line_len + 1 > cap) {
            size_t ncap = cap * 2u + line_len;
            char *nb = (char *)realloc(buf, ncap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + len, line, line_len + 1);
        len += line_len;
    }

    *out_len = len;
    return buf;
}

static char *wt_fleet_build_manifest(const WT_FleetFile *files, size_t count,
                                     const char *created_utc, size_t *out_len)
{
    char *buf = NULL;
    size_t size = 0;
    FILE *mem = NULL;
    WT_JsonWriter w;
    size_t i;

    if (out_len == NULL) {
        return NULL;
    }
    *out_len = 0;

#if defined(_MSC_VER)
    if (tmpfile_s(&mem) != 0) {
        mem = NULL;
    }
#else
    mem = tmpfile();
#endif
    if (mem == NULL) {
        return NULL;
    }

    wt_json_init(&w, mem);
    wt_json_set_compact(&w, 0);
    wt_json_begin_object(&w);
    wt_json_key(&w, "schema_version");
    wt_json_string(&w, "1.0");
    wt_json_key(&w, "kind");
    wt_json_string(&w, "wintune_fleet_pack");
    wt_json_key(&w, "tool");
    wt_json_string(&w, "wintune");
    wt_json_key(&w, "tool_version");
    wt_json_string(&w, WT_VERSION_STRING);
    wt_json_key(&w, "created_utc");
    wt_json_string(&w, created_utc != NULL ? created_utc : "");
    wt_json_key(&w, "local_only");
    wt_json_bool(&w, 1);
    wt_json_key(&w, "upload");
    wt_json_bool(&w, 0);
    wt_json_key(&w, "file_count");
    wt_json_uint64(&w, (unsigned long long)count);
    wt_json_key(&w, "files");
    wt_json_begin_array(&w);
    for (i = 0; i < count; ++i) {
        char hex[65];
        wt_sha256_hex(files[i].sha256, hex);
        wt_json_begin_object(&w);
        wt_json_key(&w, "name");
        wt_json_string(&w, files[i].zip_name);
        wt_json_key(&w, "size_bytes");
        wt_json_uint64(&w, files[i].size);
        wt_json_key(&w, "sha256");
        wt_json_string(&w, hex);
        wt_json_end_object(&w);
    }
    wt_json_end_array(&w);
    wt_json_end_object(&w);
    wt_json_finish(&w);
    fflush(mem);

    if (fseek(mem, 0, SEEK_END) != 0) {
        fclose(mem);
        return NULL;
    }
    {
        long len = ftell(mem);
        if (len < 0) {
            fclose(mem);
            return NULL;
        }
        size = (size_t)len;
    }
    if (fseek(mem, 0, SEEK_SET) != 0) {
        fclose(mem);
        return NULL;
    }
    buf = (char *)malloc(size + 1u);
    if (buf == NULL) {
        fclose(mem);
        return NULL;
    }
    if (fread(buf, 1, size, mem) != size) {
        free(buf);
        fclose(mem);
        return NULL;
    }
    buf[size] = '\0';
    fclose(mem);
    *out_len = size;
    return buf;
}

WT_Result wt_fleet_pack(const wchar_t *input_dir, const wchar_t *output_zip,
                        FILE *status_out)
{
    WT_FleetFile *files = NULL;
    size_t count = 0;
    WT_Result r;
    WT_ZipWriter zip;
    char created[32];
    char *checksums = NULL;
    size_t checksums_len = 0;
    char *manifest = NULL;
    size_t manifest_len = 0;
    unsigned char zip_digest[32];
    char zip_hex[65];
    size_t i;
    DWORD attrs;

    memset(&zip, 0, sizeof(zip));

    if (input_dir == NULL || input_dir[0] == L'\0' || output_zip == NULL ||
        output_zip[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }

    attrs = GetFileAttributesW(input_dir);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return WT_ERR_NOT_FOUND;
        }
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        return WT_ERR_WIN32;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    r = wt_fleet_collect(input_dir, &files, &count);
    if (r != WT_OK) {
        return r;
    }
    if (count == 0) {
        free(files);
        return WT_ERR_NOT_FOUND;
    }

    if (wt_now_iso8601_utc(created, sizeof(created)) != WT_OK) {
        StringCchCopyA(created, sizeof(created), "");
    }

    checksums = wt_fleet_build_checksums(files, count, &checksums_len);
    manifest = wt_fleet_build_manifest(files, count, created, &manifest_len);
    if (checksums == NULL || manifest == NULL) {
        free(checksums);
        free(manifest);
        free(files);
        return WT_ERR_OUT_OF_MEMORY;
    }

    r = wt_zip_begin(&zip, output_zip);
    if (r != WT_OK) {
        free(checksums);
        free(manifest);
        free(files);
        return r;
    }

    r = wt_zip_add_bytes(&zip, "manifest.json", manifest, manifest_len);
    if (r != WT_OK) {
        goto fail_zip;
    }
    r = wt_zip_add_bytes(&zip, "CHECKSUMS.sha256", checksums, checksums_len);
    if (r != WT_OK) {
        goto fail_zip;
    }

    for (i = 0; i < count; ++i) {
        r = wt_zip_add_path(&zip, files[i].zip_name, files[i].full_path,
                            files[i].crc32, (unsigned long)files[i].size);
        if (r != WT_OK) {
            goto fail_zip;
        }
    }

    r = wt_zip_finish(&zip);
    if (r != WT_OK) {
        free(checksums);
        free(manifest);
        free(files);
        return r;
    }

    free(checksums);
    free(manifest);

    if (wt_sha256_file(output_zip, zip_digest) == WT_OK) {
        wt_sha256_hex(zip_digest, zip_hex);
    } else {
        zip_hex[0] = '\0';
    }

    if (status_out != NULL) {
        fprintf(status_out, "WinTune fleet pack\n");
        fprintf(status_out, "  Input:   %ls\n", input_dir);
        fprintf(status_out, "  Output:  %ls\n", output_zip);
        fprintf(status_out, "  Reports: %zu JSON file(s)\n", count);
        fprintf(status_out, "  Local only (no upload)\n");
        if (zip_hex[0] != '\0') {
            fprintf(status_out, "  ZIP SHA-256: %s\n", zip_hex);
        }
        fprintf(status_out,
                "  Contents: manifest.json, CHECKSUMS.sha256, reports/*\n");
    }

    free(files);
    return WT_OK;

fail_zip:
    wt_zip_abort(&zip);
    free(checksums);
    free(manifest);
    free(files);
    return r;
}
