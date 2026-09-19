#ifndef WINTUNE_FLEET_PACK_H
#define WINTUNE_FLEET_PACK_H

#include <stdio.h>
#include <wchar.h>

#include "common/error.h"

/* Bundle *.json reports from input_dir into a local ZIP with checksums.
 * Never uploads. status_out may be NULL; otherwise receives a short text
 * summary (not used in --json mode by the CLI). */
WT_Result wt_fleet_pack(const wchar_t *input_dir, const wchar_t *output_zip,
                        FILE *status_out);

#endif /* WINTUNE_FLEET_PACK_H */
