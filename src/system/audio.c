#include "system/audio.h"

#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <objbase.h>
#include <strsafe.h>

static const CLSID WT_CLSID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID WT_IID_IMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID WT_IID_IAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const PROPERTYKEY WT_PKEY_Device_FriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14};

static int wt_audio_id_equals(const wchar_t *a, const wchar_t *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return _wcsicmp(a, b) == 0;
}

static void wt_audio_copy_default_id(IMMDeviceEnumerator *enumerator,
                                     EDataFlow flow,
                                     wchar_t *out,
                                     size_t count)
{
    IMMDevice *device = NULL;
    LPWSTR id = NULL;

    out[0] = L'\0';
    if (IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, flow, eConsole,
                                                    &device) != S_OK ||
        device == NULL) {
        return;
    }
    if (IMMDevice_GetId(device, &id) == S_OK && id != NULL) {
        StringCchCopyW(out, count, id);
        CoTaskMemFree(id);
    }
    IMMDevice_Release(device);
}

static void wt_audio_probe(IMMDevice *device, WT_AudioEndpoint *endpoint)
{
    IAudioClient *client = NULL;
    WAVEFORMATEX *format = NULL;
    HRESULT hr;

    endpoint->probed = 0;
    endpoint->exclusive = 0;
    endpoint->exclusive_only = 0;
    hr = IMMDevice_Activate(device, &WT_IID_IAudioClient, CLSCTX_ALL, NULL,
                            (void **)&client);
    if (FAILED(hr) || client == NULL) {
        return;
    }
    hr = IAudioClient_GetMixFormat(client, &format);
    if (FAILED(hr) || format == NULL) {
        IAudioClient_Release(client);
        return;
    }
    /* Shared open only. A success means the device is not held exclusively.
     * The stream is never started. */
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, 0, 0, 0,
                                 format, NULL);
    if (hr == S_OK) {
        endpoint->probed = 1;
    } else if (hr == AUDCLNT_E_DEVICE_IN_USE) {
        endpoint->probed = 1;
        endpoint->exclusive = 1;
    } else if (hr == AUDCLNT_E_EXCLUSIVE_MODE_ONLY) {
        endpoint->probed = 1;
        endpoint->exclusive = 1;
        endpoint->exclusive_only = 1;
    }
    CoTaskMemFree(format);
    IAudioClient_Release(client);
}

static void wt_audio_collect_flow(IMMDeviceEnumerator *enumerator,
                                  EDataFlow flow,
                                  const wchar_t *default_id,
                                  WT_AudioReport *out)
{
    IMMDeviceCollection *collection = NULL;
    UINT count = 0;
    UINT i;

    if (out->endpoint_count >= WT_MAX_AUDIO_ENDPOINTS) {
        return;
    }
    if (IMMDeviceEnumerator_EnumAudioEndpoints(enumerator, flow,
                                               DEVICE_STATE_ACTIVE,
                                               &collection) != S_OK ||
        collection == NULL) {
        return;
    }
    if (IMMDeviceCollection_GetCount(collection, &count) != S_OK) {
        IMMDeviceCollection_Release(collection);
        return;
    }
    for (i = 0; i < count && out->endpoint_count < WT_MAX_AUDIO_ENDPOINTS; ++i) {
        IMMDevice *device = NULL;
        IPropertyStore *store = NULL;
        PROPVARIANT name;
        LPWSTR id = NULL;
        WT_AudioEndpoint *endpoint = &out->endpoints[out->endpoint_count];

        if (IMMDeviceCollection_Item(collection, i, &device) != S_OK ||
            device == NULL) {
            continue;
        }
        ZeroMemory(endpoint, sizeof(*endpoint));
        endpoint->render = (flow == eRender) ? 1 : 0;
        PropVariantInit(&name);
        if (IMMDevice_OpenPropertyStore(device, STGM_READ, &store) == S_OK &&
            store != NULL) {
            if (SUCCEEDED(IPropertyStore_GetValue(store, &WT_PKEY_Device_FriendlyName,
                                                  &name)) &&
                name.vt == VT_LPWSTR && name.pwszVal != NULL) {
                StringCchCopyW(endpoint->name, ARRAYSIZE(endpoint->name),
                               name.pwszVal);
            }
            IPropertyStore_Release(store);
        }
        PropVariantClear(&name);
        if (IMMDevice_GetId(device, &id) == S_OK && id != NULL) {
            endpoint->default_device = wt_audio_id_equals(id, default_id);
            CoTaskMemFree(id);
        }
        wt_audio_probe(device, endpoint);
        IMMDevice_Release(device);
        if (endpoint->probed && endpoint->exclusive) {
            out->any_exclusive = 1;
        }
        out->endpoint_count++;
    }
    IMMDeviceCollection_Release(collection);
}

WT_Result wt_collect_audio(WT_AudioReport *out)
{
    HRESULT hr_init;
    int com_owned = 0;
    IMMDeviceEnumerator *enumerator = NULL;
    wchar_t default_render[512];
    wchar_t default_capture[512];
    HRESULT hr;

    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    hr_init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr_init)) {
        com_owned = 1;
    } else if (hr_init != RPC_E_CHANGED_MODE) {
        return WT_ERR_WIN32;
    }

    hr = CoCreateInstance(&WT_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &WT_IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr) || enumerator == NULL) {
        if (com_owned) {
            CoUninitialize();
        }
        return WT_ERR_NOT_SUPPORTED;
    }

    wt_audio_copy_default_id(enumerator, eRender, default_render,
                             ARRAYSIZE(default_render));
    wt_audio_copy_default_id(enumerator, eCapture, default_capture,
                             ARRAYSIZE(default_capture));
    wt_audio_collect_flow(enumerator, eRender, default_render, out);
    wt_audio_collect_flow(enumerator, eCapture, default_capture, out);
    out->available = 1;

    IMMDeviceEnumerator_Release(enumerator);
    if (com_owned) {
        CoUninitialize();
    }
    return WT_OK;
}
