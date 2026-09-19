#include "common/zip_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static unsigned long g_crc_table[256];
static int g_crc_ready = 0;

static void wt_crc32_init(void)
{
    unsigned long c;
    int n, k;

    if (g_crc_ready) {
        return;
    }
    for (n = 0; n < 256; ++n) {
        c = (unsigned long)n;
        for (k = 0; k < 8; ++k) {
            if (c & 1u) {
                c = 0xedb88320u ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        g_crc_table[n] = c;
    }
    g_crc_ready = 1;
}

unsigned long wt_crc32_update(unsigned long crc, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;

    wt_crc32_init();
    for (i = 0; i < len; ++i) {
        crc = g_crc_table[(crc ^ p[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc;
}

unsigned long wt_crc32_bytes(const void *data, size_t len)
{
    return wt_crc32_update(0xffffffffu, data, len) ^ 0xffffffffu;
}

static WT_Result wt_zip_write(WT_ZipWriter *z, const void *data, size_t len)
{
    HANDLE h;
    const unsigned char *p;
    size_t left;

    if (z == NULL || z->failed || z->file == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (len == 0) {
        return WT_OK;
    }
    if (data == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    h = (HANDLE)z->file;
    p = (const unsigned char *)data;
    left = len;
    while (left > 0) {
        DWORD chunk = (left > 0x40000000u) ? 0x40000000u : (DWORD)left;
        DWORD written = 0;
        if (!WriteFile(h, p, chunk, &written, NULL) || written != chunk) {
            z->failed = 1;
            return WT_ERR_WIN32;
        }
        p += written;
        left -= written;
        z->offset += written;
    }
    return WT_OK;
}

static void wt_zip_put_u16(unsigned char *b, unsigned short v)
{
    b[0] = (unsigned char)(v & 0xffu);
    b[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void wt_zip_put_u32(unsigned char *b, unsigned long v)
{
    b[0] = (unsigned char)(v & 0xffu);
    b[1] = (unsigned char)((v >> 8) & 0xffu);
    b[2] = (unsigned char)((v >> 16) & 0xffu);
    b[3] = (unsigned char)((v >> 24) & 0xffu);
}

static WT_Result wt_zip_grow(WT_ZipWriter *z)
{
    size_t ncap;
    WT_ZipCdEntry *n;

    if (z->entry_count < z->entry_cap) {
        return WT_OK;
    }
    ncap = (z->entry_cap == 0) ? 16u : z->entry_cap * 2u;
    n = (WT_ZipCdEntry *)realloc(z->entries, ncap * sizeof(WT_ZipCdEntry));
    if (n == NULL) {
        return WT_ERR_OUT_OF_MEMORY;
    }
    z->entries = n;
    z->entry_cap = ncap;
    return WT_OK;
}

static WT_Result wt_zip_add_local(WT_ZipWriter *z, const char *name_utf8,
                                  unsigned long crc32, unsigned long size,
                                  const void *data, size_t data_len,
                                  const wchar_t *path_opt)
{
    unsigned char hdr[30];
    size_t name_len;
    unsigned long long local_off;
    WT_Result r;
    WT_ZipCdEntry *e;

    if (z == NULL || name_utf8 == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    name_len = strlen(name_utf8);
    if (name_len == 0 || name_len >= sizeof(z->entries[0].name)) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (data != NULL && data_len != (size_t)size) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (data == NULL && path_opt == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    r = wt_zip_grow(z);
    if (r != WT_OK) {
        return r;
    }

    local_off = z->offset;
    memset(hdr, 0, sizeof(hdr));
    wt_zip_put_u32(hdr + 0, 0x04034b50u);
    wt_zip_put_u16(hdr + 4, 20);          /* version needed */
    wt_zip_put_u16(hdr + 6, 0x0800);      /* UTF-8 names */
    wt_zip_put_u16(hdr + 8, 0);           /* store */
    wt_zip_put_u16(hdr + 10, 0);          /* time */
    wt_zip_put_u16(hdr + 12, 0);          /* date */
    wt_zip_put_u32(hdr + 14, crc32);
    wt_zip_put_u32(hdr + 18, size);
    wt_zip_put_u32(hdr + 22, size);
    wt_zip_put_u16(hdr + 26, (unsigned short)name_len);
    wt_zip_put_u16(hdr + 28, 0);

    r = wt_zip_write(z, hdr, sizeof(hdr));
    if (r != WT_OK) {
        return r;
    }
    r = wt_zip_write(z, name_utf8, name_len);
    if (r != WT_OK) {
        return r;
    }

    if (data != NULL) {
        r = wt_zip_write(z, data, data_len);
        if (r != WT_OK) {
            return r;
        }
    } else {
        HANDLE in = CreateFileW(path_opt, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        unsigned char buf[64 * 1024];
        unsigned long remaining = size;

        if (in == INVALID_HANDLE_VALUE) {
            z->failed = 1;
            return WT_ERR_WIN32;
        }
        while (remaining > 0) {
            DWORD want = (remaining > sizeof(buf)) ? (DWORD)sizeof(buf)
                                                   : (DWORD)remaining;
            DWORD n = 0;
            if (!ReadFile(in, buf, want, &n, NULL) || n == 0) {
                CloseHandle(in);
                z->failed = 1;
                return WT_ERR_WIN32;
            }
            r = wt_zip_write(z, buf, n);
            if (r != WT_OK) {
                CloseHandle(in);
                return r;
            }
            remaining -= n;
        }
        CloseHandle(in);
    }

    e = &z->entries[z->entry_count++];
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name_utf8, name_len + 1);
    e->crc32 = crc32;
    e->size = size;
    e->local_offset = local_off;
    return WT_OK;
}

WT_Result wt_zip_begin(WT_ZipWriter *z, const wchar_t *path)
{
    HANDLE h;

    if (z == NULL || path == NULL || path[0] == L'\0') {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(z, 0, sizeof(*z));

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
        }
        if (err == ERROR_PATH_NOT_FOUND) {
            return WT_ERR_NOT_FOUND;
        }
        return WT_ERR_WIN32;
    }
    z->file = (void *)h;
    wcsncpy_s(z->path, sizeof(z->path) / sizeof(z->path[0]), path, _TRUNCATE);
    return WT_OK;
}

WT_Result wt_zip_add_bytes(WT_ZipWriter *z, const char *name_utf8,
                           const void *data, size_t len)
{
    unsigned long crc;

    if (len > 0xffffffffu) {
        return WT_ERR_NOT_SUPPORTED;
    }
    crc = wt_crc32_bytes(data, len);
    return wt_zip_add_local(z, name_utf8, crc, (unsigned long)len, data, len,
                            NULL);
}

WT_Result wt_zip_add_path(WT_ZipWriter *z, const char *name_utf8,
                          const wchar_t *path, unsigned long crc32,
                          unsigned long size)
{
    return wt_zip_add_local(z, name_utf8, crc32, size, NULL, 0, path);
}

WT_Result wt_zip_finish(WT_ZipWriter *z)
{
    unsigned long long cd_off;
    unsigned long long cd_size;
    size_t i;
    unsigned char eocd[22];
    WT_Result r;

    if (z == NULL || z->file == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    if (z->failed) {
        wt_zip_abort(z);
        return WT_ERR_WIN32;
    }
    if (z->entry_count > 0xffffu) {
        wt_zip_abort(z);
        return WT_ERR_NOT_SUPPORTED;
    }

    cd_off = z->offset;
    for (i = 0; i < z->entry_count; ++i) {
        unsigned char hdr[46];
        size_t name_len = strlen(z->entries[i].name);
        memset(hdr, 0, sizeof(hdr));
        wt_zip_put_u32(hdr + 0, 0x02014b50u);
        wt_zip_put_u16(hdr + 4, 20);
        wt_zip_put_u16(hdr + 6, 20);
        wt_zip_put_u16(hdr + 8, 0x0800);
        wt_zip_put_u16(hdr + 10, 0);
        wt_zip_put_u16(hdr + 12, 0);
        wt_zip_put_u16(hdr + 14, 0);
        wt_zip_put_u32(hdr + 16, z->entries[i].crc32);
        wt_zip_put_u32(hdr + 20, z->entries[i].size);
        wt_zip_put_u32(hdr + 24, z->entries[i].size);
        wt_zip_put_u16(hdr + 28, (unsigned short)name_len);
        wt_zip_put_u16(hdr + 30, 0);
        wt_zip_put_u16(hdr + 32, 0);
        wt_zip_put_u16(hdr + 34, 0);
        wt_zip_put_u16(hdr + 36, 0);
        wt_zip_put_u32(hdr + 38, 0);
        wt_zip_put_u32(hdr + 42, (unsigned long)z->entries[i].local_offset);
        r = wt_zip_write(z, hdr, sizeof(hdr));
        if (r != WT_OK) {
            wt_zip_abort(z);
            return r;
        }
        r = wt_zip_write(z, z->entries[i].name, name_len);
        if (r != WT_OK) {
            wt_zip_abort(z);
            return r;
        }
    }
    cd_size = z->offset - cd_off;

    memset(eocd, 0, sizeof(eocd));
    wt_zip_put_u32(eocd + 0, 0x06054b50u);
    wt_zip_put_u16(eocd + 4, 0);
    wt_zip_put_u16(eocd + 6, 0);
    wt_zip_put_u16(eocd + 8, (unsigned short)z->entry_count);
    wt_zip_put_u16(eocd + 10, (unsigned short)z->entry_count);
    wt_zip_put_u32(eocd + 12, (unsigned long)cd_size);
    wt_zip_put_u32(eocd + 16, (unsigned long)cd_off);
    wt_zip_put_u16(eocd + 20, 0);
    r = wt_zip_write(z, eocd, sizeof(eocd));
    if (r != WT_OK) {
        wt_zip_abort(z);
        return r;
    }

    CloseHandle((HANDLE)z->file);
    z->file = NULL;
    free(z->entries);
    z->entries = NULL;
    z->entry_count = 0;
    z->entry_cap = 0;
    return WT_OK;
}

void wt_zip_abort(WT_ZipWriter *z)
{
    if (z == NULL) {
        return;
    }
    if (z->file != NULL) {
        CloseHandle((HANDLE)z->file);
        z->file = NULL;
        if (z->path[0] != L'\0') {
            DeleteFileW(z->path);
        }
    }
    free(z->entries);
    z->entries = NULL;
    z->entry_count = 0;
    z->entry_cap = 0;
    z->failed = 1;
    z->path[0] = L'\0';
}
