#ifndef WINTUNE_APPLY_PREVIEW_H
#define WINTUNE_APPLY_PREVIEW_H

#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#include "common/error.h"
#include "actions/action_map.h"
#include "system/power.h"

/* Read-only apply preview (Phase 36). Never writes system state or rollback. */
typedef struct WT_ApplyPreview {
    wchar_t rec_id[64];
    wchar_t target_id[280];
    wchar_t action_type[64];
    wchar_t description[256];
    wchar_t previous_value[256];
    wchar_t new_value[256];
    char summary[512];
    int would_mutate;        /* 1 when apply would change the system */
    int requires_admin;
    int rollback_available;  /* 1 when a successful apply would write rollback */
    int blocked;             /* 1 when apply would be refused (protected/etc.) */
} WT_ApplyPreview;

void wt_apply_preview_init(WT_ApplyPreview *out);

/* Fills preview for a `wintune apply <id> …` request. Read-only. */
WT_Result wt_apply_preview_from_request(const WT_ApplyRequest *req,
                                        WT_ApplyPreview *out);

WT_Result wt_apply_preview_power(WT_PowerScheme target, WT_ApplyPreview *out);
WT_Result wt_apply_preview_startup_enabled(const wchar_t *id, int enable,
                                           WT_ApplyPreview *out);
WT_Result wt_apply_preview_startup_delay(const wchar_t *id,
                                         unsigned long delay_seconds,
                                         WT_ApplyPreview *out);
WT_Result wt_apply_preview_task_enabled(const wchar_t *id, int enable,
                                        WT_ApplyPreview *out);
WT_Result wt_apply_preview_task_delay(const wchar_t *id,
                                      unsigned long delay_seconds,
                                      WT_ApplyPreview *out);

void wt_apply_preview_print_text(FILE *out, const WT_ApplyPreview *p);
void wt_apply_preview_print_json(FILE *out, const WT_ApplyPreview *p);

#endif /* WINTUNE_APPLY_PREVIEW_H */
