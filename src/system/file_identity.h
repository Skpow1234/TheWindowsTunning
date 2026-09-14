#ifndef WINTUNE_FILE_IDENTITY_H
#define WINTUNE_FILE_IDENTITY_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Calm, non-scareware classification of a binary's publisher / signature.
 * Unsigned alone is never treated as malware. */
typedef enum WT_SignatureStatus {
    WT_SIG_UNKNOWN = 0,
    WT_SIG_UNSIGNED,
    WT_SIG_SIGNED,
    WT_SIG_SIGNED_MICROSOFT,
    WT_SIG_UNAVAILABLE
} WT_SignatureStatus;

typedef enum WT_PublisherOrigin {
    WT_ORIGIN_UNKNOWN = 0,
    WT_ORIGIN_MICROSOFT,
    WT_ORIGIN_THIRD_PARTY
} WT_PublisherOrigin;

typedef struct WT_FileIdentity {
    wchar_t path[MAX_PATH];
    wchar_t publisher[128]; /* CompanyName from version resources when present */
    WT_SignatureStatus signature;
    WT_PublisherOrigin origin;
    int available; /* 1 when at least path or publisher/signature was resolved */
} WT_FileIdentity;

const char *wt_signature_status_name(WT_SignatureStatus status);
const char *wt_publisher_origin_name(WT_PublisherOrigin origin);

/* Extracts the first executable/path token from a command line into `out`.
 * Handles quoted paths. Returns WT_ERR_NOT_FOUND when empty. */
WT_Result wt_identity_extract_path(const wchar_t *command_or_path,
                                   wchar_t *out,
                                   size_t out_count);

/* Fills identity for an on-disk file. Soft-fails: missing file / access denied
 * leave available=0 and signature UNAVAILABLE rather than aborting callers. */
WT_Result wt_identity_from_path(const wchar_t *path, WT_FileIdentity *out);

/* Convenience: extract path from command then resolve identity. */
WT_Result wt_identity_from_command(const wchar_t *command,
                                   WT_FileIdentity *out);

/* Resolve image path for a running PID (QueryFullProcessImageNameW). */
WT_Result wt_identity_from_pid(unsigned long pid, WT_FileIdentity *out);

#endif /* WINTUNE_FILE_IDENTITY_H */
