#include "core/report_model.h"

#include <string.h>

WT_Result wt_scan_report_init(WT_ScanReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    return WT_OK;
}
