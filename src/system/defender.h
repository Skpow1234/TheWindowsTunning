#ifndef WINTUNE_DEFENDER_H
#define WINTUNE_DEFENDER_H

#include "common/error.h"

/* Read-only Microsoft Defender status. Flags are 0 when that property was
 * not read. WinTune does not change Defender and has no disable action. */
typedef struct WT_DefenderReport {
    int available;
    int realtime_ok;
    int realtime_protection;
    int antivirus_ok;
    int antivirus_enabled;
    int service_ok;
    int service_enabled;
    int signature_age_ok;
    int signature_age_days; /* -1 when unread */
} WT_DefenderReport;

WT_Result wt_collect_defender(WT_DefenderReport *out);

#endif /* WINTUNE_DEFENDER_H */
