#include "core/report_model.h"

#include <string.h>

WT_Result wt_scan_report_init(WT_ScanReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    report->disk_read_bytes_per_sec = -1.0;
    report->disk_write_bytes_per_sec = -1.0;
    report->disk_avg_queue_length = -1.0;
    report->gpu.max_utilization_percent = -1.0;
    return WT_OK;
}
