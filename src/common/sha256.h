#ifndef WINTUNE_SHA256_H
#define WINTUNE_SHA256_H

#include <stddef.h>

#include "common/error.h"

/* Digests bytes with Windows CNG (BCrypt). out must hold 32 bytes. */
WT_Result wt_sha256_bytes(const void *data, size_t len, unsigned char out[32]);

/* Digests a file from disk. out must hold 32 bytes. */
WT_Result wt_sha256_file(const wchar_t *path, unsigned char out[32]);

/* Writes a lowercase hex string (64 chars + NUL). */
void wt_sha256_hex(const unsigned char dig[32], char out[65]);

#endif /* WINTUNE_SHA256_H */
