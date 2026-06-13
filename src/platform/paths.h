#ifndef WINTUNE_PATHS_H
#define WINTUNE_PATHS_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

WT_Result wt_paths_user_data_dir(wchar_t *out, size_t count);
WT_Result wt_paths_rollback_dir(wchar_t *out, size_t count);
WT_Result wt_paths_ensure_dir(const wchar_t *dir);
WT_Result wt_paths_program_data_dir(wchar_t *out, size_t count);
WT_Result wt_paths_last_scan_file(wchar_t *out, size_t count);

/* User reports folder: %USERPROFILE%\\Documents\\WinTune\\Reports */
WT_Result wt_paths_reports_dir(wchar_t *out, size_t count);

/* Finds the newest *.txt or *.json file in the reports dir. Returns
 * WT_ERR_NOT_FOUND when empty. */
WT_Result wt_paths_newest_report(wchar_t *out, size_t count);

#endif /* WINTUNE_PATHS_H */
