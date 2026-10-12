#ifndef WINTUNE_AUDIO_H
#define WINTUNE_AUDIO_H

#include <stddef.h>
#include <wchar.h>

#include "common/error.h"

#define WT_MAX_AUDIO_ENDPOINTS 8
#define WT_AUDIO_NAME_MAX 128

/* One active render or capture endpoint. `probed` is 0 when the shared-mode
 * open did not return a definite answer. `exclusive` is 1 when another client
 * holds the device, or the device is exclusive-only. */
typedef struct WT_AudioEndpoint {
    wchar_t name[WT_AUDIO_NAME_MAX];
    int render;          /* 1 = render, 0 = capture */
    int default_device;
    int probed;
    int exclusive;
    int exclusive_only;
} WT_AudioEndpoint;

typedef struct WT_AudioReport {
    int available;
    unsigned int endpoint_count;
    int any_exclusive;
    WT_AudioEndpoint endpoints[WT_MAX_AUDIO_ENDPOINTS];
} WT_AudioReport;

/* Enumerates active endpoints and probes shared-mode open. Does not call
 * Start, and does not change the device or its exclusive-mode policy. */
WT_Result wt_collect_audio(WT_AudioReport *out);

#endif /* WINTUNE_AUDIO_H */
