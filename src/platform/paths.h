#ifndef WINTUNE_PATHS_H
#define WINTUNE_PATHS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

/* Resolves the per-user WinTune data directory: %LOCALAPPDATA%\WinTune.
 * Does not create it. Writes a NUL-terminated path into `out`. */
WT_Result wt_paths_user_data_dir(wchar_t *out, size_t count);

/* Resolves the per-user rollback directory: %LOCALAPPDATA%\WinTune\rollback.
 * Does not create it. */
WT_Result wt_paths_rollback_dir(wchar_t *out, size_t count);

/* Ensures `dir` exists, creating intermediate components as needed.
 * Returns WT_OK if the directory exists afterwards. */
WT_Result wt_paths_ensure_dir(const wchar_t *dir);

/* Machine-wide WinTune data: %ProgramData%\\WinTune */
WT_Result wt_paths_program_data_dir(wchar_t *out, size_t count);

/* Cached service scan output: %ProgramData%\\WinTune\\last_scan.json */
WT_Result wt_paths_last_scan_file(wchar_t *out, size_t count);

#endif /* WINTUNE_PATHS_H */
