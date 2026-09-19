#include "common/sha256.h"
#include "common/zip_store.h"
#include "common/error.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static int g_failed = 0;

static void expect_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        g_failed = 1;
    }
}

static void expect_hex(const unsigned char dig[32], const char *want,
                       const char *label)
{
    char got[65];
    wt_sha256_hex(dig, got);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s:\n  got  %s\n  want %s\n", label, got, want);
        g_failed = 1;
    }
}

int main(void)
{
    unsigned char dig[32];
    wchar_t tmp_dir[MAX_PATH];
    wchar_t json_path[MAX_PATH];
    wchar_t zip_path[MAX_PATH];
    DWORD n;

    /* CRC-32 of "123456789" == 0xcbf43926 */
    expect_true(wt_crc32_bytes("123456789", 9) == 0xcbf43926u, "crc32 known");

    /* SHA-256 empty */
    expect_true(wt_sha256_bytes("", 0, dig) == WT_OK, "sha empty ok");
    expect_hex(dig,
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
               "sha empty");

    /* SHA-256 "abc" */
    expect_true(wt_sha256_bytes("abc", 3, dig) == WT_OK, "sha abc ok");
    expect_hex(dig,
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
               "sha abc");

    /* Minimal ZIP round-trip: write STORE zip and check local signature. */
    n = GetTempPathW(MAX_PATH, tmp_dir);
    expect_true(n > 0 && n < MAX_PATH, "temp path");
    if (n > 0 && n < MAX_PATH) {
        WT_ZipWriter z;
        HANDLE h;
        unsigned char hdr[4];
        DWORD read = 0;

        _snwprintf_s(zip_path, MAX_PATH, _TRUNCATE, L"%ls\\wintune_fleet_test.zip",
                     tmp_dir);
        _snwprintf_s(json_path, MAX_PATH, _TRUNCATE, L"%ls\\wintune_fleet_test.json",
                     tmp_dir);

        {
            HANDLE jf = CreateFileW(json_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
            const char payload[] = "{\"ok\":true}\n";
            DWORD w = 0;
            expect_true(jf != INVALID_HANDLE_VALUE, "create json");
            if (jf != INVALID_HANDLE_VALUE) {
                WriteFile(jf, payload, (DWORD)strlen(payload), &w, NULL);
                CloseHandle(jf);
            }
        }

        expect_true(wt_zip_begin(&z, zip_path) == WT_OK, "zip begin");
        expect_true(wt_zip_add_bytes(&z, "hello.txt", "hi\n", 3) == WT_OK,
                    "zip add");
        expect_true(wt_zip_finish(&z) == WT_OK, "zip finish");

        h = CreateFileW(zip_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        expect_true(h != INVALID_HANDLE_VALUE, "open zip");
        if (h != INVALID_HANDLE_VALUE) {
            ReadFile(h, hdr, 4, &read, NULL);
            CloseHandle(h);
            expect_true(read == 4 && hdr[0] == 0x50 && hdr[1] == 0x4b &&
                            hdr[2] == 0x03 && hdr[3] == 0x04,
                        "zip local signature");
        }

        DeleteFileW(zip_path);
        DeleteFileW(json_path);
    }

    return g_failed ? 1 : 0;
}
