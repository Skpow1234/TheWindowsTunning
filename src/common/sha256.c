#include "common/sha256.h"

#include <stdio.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

WT_Result wt_sha256_bytes(const void *data, size_t len, unsigned char out[32])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS st;
    DWORD obj_len = 0;
    DWORD cb = 0;
    unsigned char *obj = NULL;
    WT_Result rc = WT_OK;

    if (out == NULL || (data == NULL && len > 0)) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(st)) {
        return WT_ERR_WIN32;
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

    if (len > 0) {
        st = BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0);
        if (!NT_SUCCESS(st)) {
            rc = WT_ERR_WIN32;
            goto done;
        }
    }

    st = BCryptFinishHash(hash, out, 32, 0);
    if (!NT_SUCCESS(st)) {
        rc = WT_ERR_WIN32;
    }

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
    return rc;
}

WT_Result wt_sha256_file(const wchar_t *path, unsigned char out[32])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    NTSTATUS st;
    DWORD obj_len = 0;
    DWORD cb = 0;
    unsigned char *obj = NULL;
    unsigned char buf[64 * 1024];
    WT_Result rc = WT_OK;

    if (path == NULL || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return WT_ERR_NOT_FOUND;
        }
        if (err == ERROR_ACCESS_DENIED) {
            return WT_ERR_ACCESS_DENIED;
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
        if (!ReadFile(file, buf, (DWORD)sizeof(buf), &n, NULL)) {
            rc = WT_ERR_WIN32;
            goto done;
        }
        if (n == 0) {
            break;
        }
        st = BCryptHashData(hash, buf, n, 0);
        if (!NT_SUCCESS(st)) {
            rc = WT_ERR_WIN32;
            goto done;
        }
    }

    st = BCryptFinishHash(hash, out, 32, 0);
    if (!NT_SUCCESS(st)) {
        rc = WT_ERR_WIN32;
    }

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
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    return rc;
}

void wt_sha256_hex(const unsigned char dig[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (out == NULL) {
        return;
    }
    if (dig == NULL) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i < 32; ++i) {
        out[i * 2] = hex[(dig[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[dig[i] & 0x0f];
    }
    out[64] = '\0';
}
