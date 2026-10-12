#include "system/search.h"

#define COBJMACROS
#include <windows.h>
#include <winsvc.h>
#include <searchapi.h>
#include <objbase.h>

static const CLSID WT_CLSID_CSearchManager = {
    0x7D096C5F, 0xAC08, 0x4f1f, {0xBE, 0xB7, 0x5C, 0x22, 0xC5, 0x17, 0xCE, 0x39}};
static const IID WT_IID_ISearchManager = {
    0xAB310581, 0xAC80, 0x11D1, {0x8D, 0xF3, 0x00, 0xC0, 0x4F, 0xB6, 0xEF, 0x69}};

const char *wt_search_status_token(int status)
{
    switch (status) {
    case CATALOG_STATUS_IDLE:
        return "idle";
    case CATALOG_STATUS_PAUSED:
        return "paused";
    case CATALOG_STATUS_RECOVERING:
        return "recovering";
    case CATALOG_STATUS_FULL_CRAWL:
        return "full_crawl";
    case CATALOG_STATUS_INCREMENTAL_CRAWL:
        return "incremental_crawl";
    case CATALOG_STATUS_PROCESSING_NOTIFICATIONS:
        return "processing_notifications";
    case CATALOG_STATUS_SHUTTING_DOWN:
        return "shutting_down";
    default:
        return "unknown";
    }
}

const char *wt_search_pause_token(int reason)
{
    switch (reason) {
    case CATALOG_PAUSED_REASON_NONE:
        return "none";
    case CATALOG_PAUSED_REASON_HIGH_IO:
        return "high_io";
    case CATALOG_PAUSED_REASON_HIGH_CPU:
        return "high_cpu";
    case CATALOG_PAUSED_REASON_HIGH_NTF_RATE:
        return "high_notification_rate";
    case CATALOG_PAUSED_REASON_LOW_BATTERY:
        return "low_battery";
    case CATALOG_PAUSED_REASON_LOW_MEMORY:
        return "low_memory";
    case CATALOG_PAUSED_REASON_LOW_DISK:
        return "low_disk";
    case CATALOG_PAUSED_REASON_DELAYED_RECOVERY:
        return "delayed_recovery";
    case CATALOG_PAUSED_REASON_USER_ACTIVE:
        return "user_active";
    case CATALOG_PAUSED_REASON_EXTERNAL:
        return "external";
    case CATALOG_PAUSED_REASON_UPGRADING:
        return "upgrading";
    default:
        return "unknown";
    }
}

int wt_search_backlog_notable(const WT_SearchReport *report)
{
    int crawling;
    int queued;

    if (report == NULL || !report->available) {
        return 0;
    }
    crawling = report->status_ok &&
               (report->status == CATALOG_STATUS_RECOVERING ||
                report->status == CATALOG_STATUS_FULL_CRAWL);
    queued = report->backlog_ok && report->backlog_items >= 100;
    return crawling || queued;
}

static void wt_search_read_service(WT_SearchReport *out)
{
    SC_HANDLE scm;
    SC_HANDLE service;
    SERVICE_STATUS status;

    out->service_running = -1;
    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return;
    }
    service = OpenServiceW(scm, L"WSearch", SERVICE_QUERY_STATUS);
    if (service == NULL) {
        CloseServiceHandle(scm);
        return;
    }
    if (QueryServiceStatus(service, &status)) {
        out->service_running =
            (status.dwCurrentState == SERVICE_RUNNING ||
             status.dwCurrentState == SERVICE_START_PENDING)
                ? 1
                : 0;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
}

static void wt_search_read_catalog(WT_SearchReport *out)
{
    HRESULT hr_init;
    int com_owned = 0;
    ISearchManager *manager = NULL;
    ISearchCatalogManager *catalog = NULL;
    CatalogStatus status = CATALOG_STATUS_IDLE;
    CatalogPausedReason reason = CATALOG_PAUSED_REASON_NONE;
    LONG indexed = 0;
    LONG incremental = 0;
    LONG notifications = 0;
    LONG high_priority = 0;
    HRESULT hr;

    hr_init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr_init)) {
        com_owned = 1;
    } else if (hr_init != RPC_E_CHANGED_MODE) {
        return;
    }

    hr = CoCreateInstance(&WT_CLSID_CSearchManager, NULL, CLSCTX_ALL,
                          &WT_IID_ISearchManager, (void **)&manager);
    if (FAILED(hr) || manager == NULL) {
        if (com_owned) {
            CoUninitialize();
        }
        return;
    }
    hr = ISearchManager_GetCatalog(manager, L"SystemIndex", &catalog);
    if (FAILED(hr) || catalog == NULL) {
        ISearchManager_Release(manager);
        if (com_owned) {
            CoUninitialize();
        }
        return;
    }

    if (ISearchCatalogManager_GetCatalogStatus(catalog, &status, &reason) ==
        S_OK) {
        out->status_ok = 1;
        out->status = (int)status;
        if (status == CATALOG_STATUS_PAUSED) {
            out->paused_reason = (int)reason;
        }
    }
    if (ISearchCatalogManager_NumberOfItems(catalog, &indexed) == S_OK &&
        indexed >= 0) {
        out->items_ok = 1;
        out->items_indexed = indexed;
    }
    if (ISearchCatalogManager_NumberOfItemsToIndex(
            catalog, &incremental, &notifications, &high_priority) == S_OK &&
        incremental >= 0 && notifications >= 0 && high_priority >= 0) {
        unsigned long long sum = (unsigned long long)incremental +
                                 (unsigned long long)notifications +
                                 (unsigned long long)high_priority;
        out->backlog_ok = 1;
        out->incremental_count = incremental;
        out->notification_count = notifications;
        out->high_priority_count = high_priority;
        out->backlog_items =
            sum > 2147483647ull ? 2147483647L : (long)sum;
    }
    out->available = (out->status_ok || out->items_ok || out->backlog_ok) ? 1 : 0;

    ISearchCatalogManager_Release(catalog);
    ISearchManager_Release(manager);
    if (com_owned) {
        CoUninitialize();
    }
}

WT_Result wt_collect_search(WT_SearchReport *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    out->service_running = -1;
    out->status = -1;
    out->paused_reason = -1;
    out->items_indexed = -1;
    out->incremental_count = -1;
    out->notification_count = -1;
    out->high_priority_count = -1;
    out->backlog_items = -1;

    wt_search_read_service(out);
    wt_search_read_catalog(out);
    return WT_OK;
}
