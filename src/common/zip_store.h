#ifndef WINTUNE_ZIP_STORE_H
#define WINTUNE_ZIP_STORE_H

#include <stddef.h>

#include "common/error.h"

/* Minimal ZIP writer using STORE (no compression). UTF-8 entry names. */

unsigned long wt_crc32_bytes(const void *data, size_t len);
unsigned long wt_crc32_update(unsigned long crc, const void *data, size_t len);

typedef struct WT_ZipCdEntry {
    char name[260];
    unsigned long crc32;
    unsigned long size;
    unsigned long long local_offset;
} WT_ZipCdEntry;

typedef struct WT_ZipWriter {
    void *file; /* HANDLE stored as void* to keep header free of windows.h */
    wchar_t path[520];
    unsigned long long offset;
    WT_ZipCdEntry *entries;
    size_t entry_count;
    size_t entry_cap;
    int failed;
} WT_ZipWriter;

WT_Result wt_zip_begin(WT_ZipWriter *z, const wchar_t *path);
WT_Result wt_zip_add_bytes(WT_ZipWriter *z, const char *name_utf8,
                           const void *data, size_t len);
/* Streams a file from disk; crc32/size must match the file contents. */
WT_Result wt_zip_add_path(WT_ZipWriter *z, const char *name_utf8,
                          const wchar_t *path, unsigned long crc32,
                          unsigned long size);
WT_Result wt_zip_finish(WT_ZipWriter *z);
void wt_zip_abort(WT_ZipWriter *z);

#endif /* WINTUNE_ZIP_STORE_H */
