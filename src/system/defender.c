#include "system/defender.h"

#define COBJMACROS
#include <windows.h>
#include <wbemidl.h>
#include <objbase.h>

static const CLSID WT_CLSID_WbemLocator = {
    0x4590f811, 0x1d3a, 0x11d0, {0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};
static const IID WT_IID_IWbemLocator = {
    0xdc12a687, 0x737f, 0x11cf, {0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};

static int wt_defender_age_days(const SYSTEMTIME *stamp, int *days)
{
    FILETIME stamp_ft;
    FILETIME now_ft;
    ULARGE_INTEGER stamp_u;
    ULARGE_INTEGER now_u;
    SYSTEMTIME now;
    unsigned long long diff;

    if (!SystemTimeToFileTime(stamp, &stamp_ft)) {
        return 0;
    }
    GetSystemTime(&now);
    if (!SystemTimeToFileTime(&now, &now_ft)) {
        return 0;
    }
    stamp_u.LowPart = stamp_ft.dwLowDateTime;
    stamp_u.HighPart = stamp_ft.dwHighDateTime;
    now_u.LowPart = now_ft.dwLowDateTime;
    now_u.HighPart = now_ft.dwHighDateTime;
    if (now_u.QuadPart < stamp_u.QuadPart) {
        *days = 0;
        return 1;
    }
    diff = now_u.QuadPart - stamp_u.QuadPart;
    *days = (int)(diff / (24ull * 60ull * 60ull * 10000000ull));
    return 1;
}

static unsigned int wt_defender_digits(const wchar_t *text, int start, int count)
{
    unsigned int value = 0;
    int i;

    for (i = 0; i < count; ++i) {
        value = value * 10u + (unsigned int)(text[start + i] - L'0');
    }
    return value;
}

static int wt_defender_parse_cim(const wchar_t *text, SYSTEMTIME *out)
{
    int i;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; i < 14; ++i) {
        if (text[i] < L'0' || text[i] > L'9') {
            return 0;
        }
    }
    year = wt_defender_digits(text, 0, 4);
    month = wt_defender_digits(text, 4, 2);
    day = wt_defender_digits(text, 6, 2);
    hour = wt_defender_digits(text, 8, 2);
    minute = wt_defender_digits(text, 10, 2);
    second = wt_defender_digits(text, 12, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 59) {
        return 0;
    }
    ZeroMemory(out, sizeof(*out));
    out->wYear = (WORD)year;
    out->wMonth = (WORD)month;
    out->wDay = (WORD)day;
    out->wHour = (WORD)hour;
    out->wMinute = (WORD)minute;
    out->wSecond = (WORD)second;
    return 1;
}

static void wt_defender_read_bool(IWbemClassObject *object, const wchar_t *name,
                                  int *ok, int *value)
{
    VARIANT property;

    *ok = 0;
    *value = 0;
    VariantInit(&property);
    if (FAILED(IWbemClassObject_Get(object, name, 0, &property, NULL, NULL))) {
        VariantClear(&property);
        return;
    }
    if (property.vt == VT_BOOL) {
        *ok = 1;
        *value = (property.boolVal != VARIANT_FALSE) ? 1 : 0;
    } else if (property.vt == VT_I4 || property.vt == VT_UI4 ||
               property.vt == VT_INT) {
        *ok = 1;
        *value = (property.lVal != 0) ? 1 : 0;
    }
    VariantClear(&property);
}

static void wt_defender_read_signature_age(IWbemClassObject *object,
                                           WT_DefenderReport *out)
{
    VARIANT property;
    SYSTEMTIME stamp;

    out->signature_age_ok = 0;
    out->signature_age_days = -1;
    VariantInit(&property);
    if (FAILED(IWbemClassObject_Get(object, L"AntivirusSignatureLastUpdated", 0,
                                    &property, NULL, NULL))) {
        VariantClear(&property);
        return;
    }
    ZeroMemory(&stamp, sizeof(stamp));
    if (property.vt == VT_BSTR && property.bstrVal != NULL &&
        wt_defender_parse_cim(property.bstrVal, &stamp) &&
        wt_defender_age_days(&stamp, &out->signature_age_days)) {
        out->signature_age_ok = 1;
    } else if (property.vt == VT_DATE &&
               VariantTimeToSystemTime(property.date, &stamp) &&
               wt_defender_age_days(&stamp, &out->signature_age_days)) {
        out->signature_age_ok = 1;
    }
    VariantClear(&property);
}

static void wt_defender_read_status(WT_DefenderReport *out)
{
    HRESULT hr_init;
    int com_owned = 0;
    IWbemLocator *locator = NULL;
    IWbemServices *services = NULL;
    IEnumWbemClassObject *enumerator = NULL;
    IWbemClassObject *object = NULL;
    BSTR name_space = NULL;
    BSTR language = NULL;
    BSTR query = NULL;
    ULONG returned = 0;
    HRESULT hr;

    hr_init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr_init)) {
        com_owned = 1;
    } else if (hr_init != RPC_E_CHANGED_MODE) {
        return;
    }

    hr = CoCreateInstance(&WT_CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                          &WT_IID_IWbemLocator, (void **)&locator);
    if (FAILED(hr) || locator == NULL) {
        goto done;
    }
    name_space = SysAllocString(L"root\\Microsoft\\Windows\\Defender");
    if (name_space == NULL) {
        goto done;
    }
    hr = IWbemLocator_ConnectServer(locator, name_space, NULL, NULL, NULL, 0,
                                    NULL, NULL, &services);
    if (FAILED(hr) || services == NULL) {
        goto done;
    }
    (void)CoSetProxyBlanket((IUnknown *)services, RPC_C_AUTHN_WINNT,
                            RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

    language = SysAllocString(L"WQL");
    query = SysAllocString(
        L"SELECT RealTimeProtectionEnabled, AntivirusEnabled, "
        L"AMServiceEnabled, AntivirusSignatureLastUpdated "
        L"FROM MSFT_MpComputerStatus");
    if (language == NULL || query == NULL) {
        goto done;
    }
    hr = IWbemServices_ExecQuery(services, language, query,
                                 WBEM_FLAG_FORWARD_ONLY |
                                     WBEM_FLAG_RETURN_IMMEDIATELY,
                                 NULL, &enumerator);
    if (FAILED(hr) || enumerator == NULL) {
        goto done;
    }
    (void)CoSetProxyBlanket((IUnknown *)enumerator, RPC_C_AUTHN_WINNT,
                            RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    hr = IEnumWbemClassObject_Next(enumerator, 5000, 1, &object, &returned);
    if (FAILED(hr) || returned == 0 || object == NULL) {
        goto done;
    }

    wt_defender_read_bool(object, L"RealTimeProtectionEnabled",
                          &out->realtime_ok, &out->realtime_protection);
    wt_defender_read_bool(object, L"AntivirusEnabled", &out->antivirus_ok,
                          &out->antivirus_enabled);
    wt_defender_read_bool(object, L"AMServiceEnabled", &out->service_ok,
                          &out->service_enabled);
    wt_defender_read_signature_age(object, out);
    out->available = out->realtime_ok ? 1 : 0;

done:
    if (object != NULL) {
        IWbemClassObject_Release(object);
    }
    if (enumerator != NULL) {
        IEnumWbemClassObject_Release(enumerator);
    }
    if (services != NULL) {
        IWbemServices_Release(services);
    }
    if (locator != NULL) {
        IWbemLocator_Release(locator);
    }
    if (query != NULL) {
        SysFreeString(query);
    }
    if (language != NULL) {
        SysFreeString(language);
    }
    if (name_space != NULL) {
        SysFreeString(name_space);
    }
    if (com_owned) {
        CoUninitialize();
    }
}

WT_Result wt_collect_defender(WT_DefenderReport *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    out->signature_age_days = -1;
    wt_defender_read_status(out);
    return WT_OK;
}
