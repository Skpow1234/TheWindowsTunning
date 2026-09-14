#include "core/impact_score.h"

#include "system/file_identity.h"

#include <string.h>
#include <wchar.h>

/* Thresholds are explicit so scores stay explainable and unit-testable.
 * See docs/metrics.md "Impact scoring v2". */

#define WT_IMPACT_WS_MED_BYTES   (200ull * 1024ull * 1024ull)
#define WT_IMPACT_WS_HIGH_BYTES  (500ull * 1024ull * 1024ull)
#define WT_IMPACT_DISK_MED_BPS   (1.0 * 1024.0 * 1024.0)
#define WT_IMPACT_DISK_HIGH_BPS  (5.0 * 1024.0 * 1024.0)

static int wt_clamp_int(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void wt_impact_tolower_inplace(wchar_t *s)
{
    if (s == NULL) {
        return;
    }
    for (; *s != L'\0'; ++s) {
        if (*s >= L'A' && *s <= L'Z') {
            *s = (wchar_t)(*s - L'A' + L'a');
        }
    }
}

int wt_impact_name_heuristic_hit(const wchar_t *name, const wchar_t *command)
{
    static const wchar_t *heavy[] = {
        L"docker", L"teams", L"onedrive", L"steam", L"epicgames",
        L"discord", L"spotify", L"adobe", L"creative cloud", L"dropbox",
        L"slack", L"java", L"razer", L"nvidia", L"asus", L"icloud",
        L"notion", L"opera", L"chrome", L"msedge", L"lghub"
    };

    wchar_t hay[1200];
    hay[0] = L'\0';
    if (name != NULL) {
        wcsncpy_s(hay, 1200, name, _TRUNCATE);
    }
    size_t used = wcslen(hay);
    if (command != NULL && used + 1 < 1200) {
        hay[used++] = L' ';
        hay[used] = L'\0';
        wcsncpy_s(hay + used, 1200 - used, command, _TRUNCATE);
    }
    wt_impact_tolower_inplace(hay);

    for (size_t i = 0; i < sizeof(heavy) / sizeof(heavy[0]); ++i) {
        if (wcsstr(hay, heavy[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

WT_StartupImpact wt_impact_score_to_band(int score, unsigned int evidence)
{
    if (score <= 0 && evidence == 0) {
        return WT_STARTUP_IMPACT_UNKNOWN;
    }
    if (score >= 65) {
        return WT_STARTUP_IMPACT_HIGH;
    }
    if (score >= 35) {
        return WT_STARTUP_IMPACT_MEDIUM;
    }
    if (score > 0 || evidence != 0) {
        return WT_STARTUP_IMPACT_LOW;
    }
    return WT_STARTUP_IMPACT_UNKNOWN;
}

void wt_impact_score_compute(const WT_ImpactInput *in, WT_ImpactScore *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (in == NULL) {
        out->band = WT_STARTUP_IMPACT_UNKNOWN;
        return;
    }

    int score = 0;
    int conf = 0;
    unsigned int ev = 0;

    if (in->measured_available) {
        ev |= WT_IMPACT_EV_MEASURED;
        conf += 50;
        if (in->measured_ms >= 10000u) {
            score += 60;
        } else if (in->measured_ms >= 3000u) {
            score += 45;
        } else if (in->measured_ms >= 1000u) {
            score += 25;
        } else if (in->measured_ms > 0u) {
            score += 10;
        }
    }

    if (wt_impact_name_heuristic_hit(in->name, in->command)) {
        ev |= WT_IMPACT_EV_HEURISTIC;
        conf += 10;
        score += in->measured_available ? 8 : 20;
    }

    if (in->microsoft_protected || in->origin == WT_ORIGIN_MICROSOFT) {
        ev |= WT_IMPACT_EV_PUBLISHER;
        conf += 10;
        score -= 15;
    } else if (in->origin == WT_ORIGIN_THIRD_PARTY) {
        ev |= WT_IMPACT_EV_PUBLISHER;
        conf += 10;
        score += 5;
    }

    if (in->unusual_location) {
        ev |= WT_IMPACT_EV_LOCATION;
        conf += 10;
        score += 10;
    }

    int runtime = 0;
    if (in->cpu_percent >= 15.0) {
        runtime += 20;
    } else if (in->cpu_percent >= 5.0) {
        runtime += 10;
    }
    if (in->working_set_bytes >= WT_IMPACT_WS_HIGH_BYTES) {
        runtime += 20;
    } else if (in->working_set_bytes >= WT_IMPACT_WS_MED_BYTES) {
        runtime += 10;
    }
    if (in->disk_bytes_per_sec >= WT_IMPACT_DISK_HIGH_BPS) {
        runtime += 15;
    } else if (in->disk_bytes_per_sec >= WT_IMPACT_DISK_MED_BPS) {
        runtime += 10;
    }
    if (runtime > 0) {
        ev |= WT_IMPACT_EV_RUNTIME;
        conf += 20;
        score += runtime;
    }

    score = wt_clamp_int(score, 0, 100);
    conf = wt_clamp_int(conf, 0, 95);

    out->score = score;
    out->confidence_percent = conf;
    out->evidence = ev;
    out->band = wt_impact_score_to_band(score, ev);
}

void wt_impact_input_from_startup(const WT_StartupEntry *e, WT_ImpactInput *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cpu_percent = -1.0;
    out->disk_bytes_per_sec = -1.0;
    if (e == NULL) {
        return;
    }
    out->name = e->name;
    out->command = e->command;
    out->measured_ms = e->measured_ms;
    out->measured_available = e->measured_available;
    out->origin = e->identity.origin;
    out->unusual_location = e->identity.unusual_location;
    out->microsoft_protected = 0;
}

void wt_impact_apply_to_startup(WT_StartupEntry *e, const WT_ImpactScore *score)
{
    if (e == NULL || score == NULL) {
        return;
    }
    e->impact = score->band;
    e->impact_score = score->score;
    e->impact_confidence = score->confidence_percent;
}

void wt_impact_input_from_task(const WT_ScheduledTask *t, WT_ImpactInput *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->cpu_percent = -1.0;
    out->disk_bytes_per_sec = -1.0;
    if (t == NULL) {
        return;
    }
    out->name = t->name;
    out->command = t->command;
    out->measured_ms = t->measured_ms;
    out->measured_available = t->measured_available;
    out->microsoft_protected = t->is_microsoft;
    out->origin = t->is_microsoft ? WT_ORIGIN_MICROSOFT : WT_ORIGIN_UNKNOWN;
    if (!t->is_microsoft && t->author[0] != L'\0' &&
        wcsstr(t->author, L"Microsoft") == NULL) {
        out->origin = WT_ORIGIN_THIRD_PARTY;
    }
    out->unusual_location = 0;
}

void wt_impact_apply_to_task(WT_ScheduledTask *t, const WT_ImpactScore *score)
{
    if (t == NULL || score == NULL) {
        return;
    }
    t->impact = score->band;
    t->impact_score = score->score;
    t->impact_confidence = score->confidence_percent;
}

static int wt_impact_exe_basename(const wchar_t *command, wchar_t *out,
                                  size_t out_count)
{
    if (out == NULL || out_count == 0) {
        return 0;
    }
    out[0] = L'\0';
    wchar_t path[MAX_PATH];
    if (wt_identity_extract_path(command, path, MAX_PATH) != WT_OK) {
        return 0;
    }
    const wchar_t *base = path;
    for (const wchar_t *p = path; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            base = p + 1;
        }
    }
    if (base[0] == L'\0') {
        return 0;
    }
    wcsncpy_s(out, out_count, base, _TRUNCATE);
    return 1;
}

void wt_impact_attach_runtime(WT_ImpactInput *in, const WT_ProcessInfo *procs,
                              size_t count)
{
    if (in == NULL || procs == NULL || count == 0) {
        return;
    }

    wchar_t exe[WT_PROCESS_NAME_MAX];
    if (!wt_impact_exe_basename(in->command, exe, WT_PROCESS_NAME_MAX)) {
        return;
    }

    double best_cpu = -1.0;
    unsigned long long best_ws = 0;
    double best_disk = -1.0;
    int matched = 0;

    for (size_t i = 0; i < count; ++i) {
        if (_wcsicmp(procs[i].name, exe) != 0) {
            continue;
        }
        matched = 1;
        if (procs[i].cpu_percent > best_cpu) {
            best_cpu = procs[i].cpu_percent;
        }
        if (procs[i].working_set_bytes > best_ws) {
            best_ws = procs[i].working_set_bytes;
        }
        double disk = 0.0;
        int have_disk = 0;
        if (procs[i].disk_read_bytes_per_sec >= 0.0) {
            disk += procs[i].disk_read_bytes_per_sec;
            have_disk = 1;
        }
        if (procs[i].disk_write_bytes_per_sec >= 0.0) {
            disk += procs[i].disk_write_bytes_per_sec;
            have_disk = 1;
        }
        if (have_disk && disk > best_disk) {
            best_disk = disk;
        }
    }

    if (!matched) {
        return;
    }
    in->cpu_percent = best_cpu;
    in->working_set_bytes = best_ws;
    in->disk_bytes_per_sec = best_disk;
}
