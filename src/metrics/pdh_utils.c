#include "metrics/pdh_utils.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

WT_Result wt_pdh_sample_single(const wchar_t *counter_path,
                               unsigned int interval_ms,
                               double *out_value)
{
    if (counter_path == NULL || out_value == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    *out_value = 0.0;

    PDH_HQUERY query = NULL;
    PDH_HCOUNTER counter = NULL;
    WT_Result result = WT_ERR_PDH;

    if (PdhOpenQueryW(NULL, 0, &query) != ERROR_SUCCESS) {
        return WT_ERR_PDH;
    }

    /* English counter path so this works on localized Windows installs. */
    if (PdhAddEnglishCounterW(query, counter_path, 0, &counter) != ERROR_SUCCESS) {
        goto cleanup;
    }

    /* Rate counters need a baseline collection, a wait, then a second one. */
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }
    Sleep(interval_ms);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        goto cleanup;
    }

    PDH_FMT_COUNTERVALUE value;
    DWORD value_type = 0;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &value_type, &value)
            != ERROR_SUCCESS) {
        goto cleanup;
    }

    *out_value = value.doubleValue;
    result = WT_OK;

cleanup:
    if (query != NULL) {
        PdhCloseQuery(query);
    }
    return result;
}
