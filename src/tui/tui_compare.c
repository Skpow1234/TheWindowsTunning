#include "tui/tui_compare.h"
#include "platform/paths.h"

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void wt_tui_compare_snap_clear(WT_TuiCompareSnap *s)
{
    if (s == NULL) {
        return;
    }
    ZeroMemory(s, sizeof(*s));
    s->cpu_percent = -1.0;
    s->mem_percent = -1.0;
    s->disk_percent = -1.0;
    s->net_rx_bps = -1.0;
    s->net_tx_bps = -1.0;
}

void wt_tui_compare_capture(WT_TuiCompareSnap *out,
                            double cpu, double mem_pct, double disk,
                            double net_rx, double net_tx, int net_ok,
                            unsigned long long mem_used,
                            unsigned long long mem_total, int mem_ok)
{
    if (out == NULL) {
        return;
    }
    wt_tui_compare_snap_clear(out);
    out->valid = 1;

    SYSTEMTIME ut;
    GetSystemTime(&ut);
    StringCchPrintfA(out->captured_utc, sizeof(out->captured_utc),
                     "%04u-%02u-%02uT%02u:%02u:%02uZ",
                     ut.wYear, ut.wMonth, ut.wDay,
                     ut.wHour, ut.wMinute, ut.wSecond);

    out->cpu_percent = cpu;
    out->disk_percent = disk;
    if (mem_ok) {
        out->mem_percent = mem_pct;
        out->mem_used_bytes = mem_used;
        out->mem_total_bytes = mem_total;
    }
    if (net_ok) {
        out->net_rx_bps = net_rx;
        out->net_tx_bps = net_tx;
    }
}

void wt_tui_compare_delta(const WT_TuiComparePair *pair,
                          WT_TuiCompareDelta *out)
{
    if (out == NULL) {
        return;
    }
    ZeroMemory(out, sizeof(*out));
    if (pair == NULL || !pair->before.valid || !pair->after.valid) {
        return;
    }
    out->both_valid = 1;
    if (pair->before.cpu_percent >= 0.0 && pair->after.cpu_percent >= 0.0) {
        out->cpu_pp = pair->after.cpu_percent - pair->before.cpu_percent;
    }
    if (pair->before.mem_percent >= 0.0 && pair->after.mem_percent >= 0.0) {
        out->mem_pp = pair->after.mem_percent - pair->before.mem_percent;
    }
    if (pair->before.disk_percent >= 0.0 && pair->after.disk_percent >= 0.0) {
        out->disk_pp = pair->after.disk_percent - pair->before.disk_percent;
    }
    if (pair->before.net_rx_bps >= 0.0 && pair->after.net_rx_bps >= 0.0) {
        out->net_rx_bps = pair->after.net_rx_bps - pair->before.net_rx_bps;
    }
    if (pair->before.net_tx_bps >= 0.0 && pair->after.net_tx_bps >= 0.0) {
        out->net_tx_bps = pair->after.net_tx_bps - pair->before.net_tx_bps;
    }
    if (pair->before.mem_used_bytes > 0 || pair->after.mem_used_bytes > 0) {
        out->mem_used_bytes =
            (long long)pair->after.mem_used_bytes -
            (long long)pair->before.mem_used_bytes;
    }
}

static WT_Result wt_tui_compare_dir(wchar_t *out, size_t count)
{
    wchar_t base[MAX_PATH];
    WT_Result r = wt_paths_user_data_dir(base, ARRAYSIZE(base));
    if (r != WT_OK) {
        return r;
    }
    if (FAILED(StringCchPrintfW(out, count, L"%s\\compare", base))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }
    return wt_paths_ensure_dir(out);
}

static int wt_tui_compare_slot_ok(const wchar_t *slot)
{
    return slot != NULL &&
           (_wcsicmp(slot, L"before") == 0 || _wcsicmp(slot, L"after") == 0);
}

WT_Result wt_tui_compare_save_slot(const wchar_t *slot,
                                   const WT_TuiCompareSnap *snap)
{
    if (!wt_tui_compare_slot_ok(slot) || snap == NULL || !snap->valid) {
        return WT_ERR_INVALID_ARGUMENT;
    }

    wchar_t dir[MAX_PATH];
    WT_Result r = wt_tui_compare_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }

    wchar_t path[MAX_PATH];
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\%s.json", dir,
                                slot))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"wb") != 0 || f == NULL) {
        return WT_ERR_WIN32;
    }

    fputs("{\n", f);
    fprintf(f, "  \"slot\": \"%ls\",\n", slot);
    fprintf(f, "  \"captured_utc\": \"%s\",\n", snap->captured_utc);
    if (snap->cpu_percent >= 0.0) {
        fprintf(f, "  \"cpu_percent\": %.3f,\n", snap->cpu_percent);
    } else {
        fputs("  \"cpu_percent\": null,\n", f);
    }
    if (snap->mem_percent >= 0.0) {
        fprintf(f, "  \"mem_percent\": %.3f,\n", snap->mem_percent);
    } else {
        fputs("  \"mem_percent\": null,\n", f);
    }
    if (snap->disk_percent >= 0.0) {
        fprintf(f, "  \"disk_percent\": %.3f,\n", snap->disk_percent);
    } else {
        fputs("  \"disk_percent\": null,\n", f);
    }
    if (snap->net_rx_bps >= 0.0) {
        fprintf(f, "  \"net_rx_bps\": %.3f,\n", snap->net_rx_bps);
    } else {
        fputs("  \"net_rx_bps\": null,\n", f);
    }
    if (snap->net_tx_bps >= 0.0) {
        fprintf(f, "  \"net_tx_bps\": %.3f,\n", snap->net_tx_bps);
    } else {
        fputs("  \"net_tx_bps\": null,\n", f);
    }
    fprintf(f, "  \"mem_used_bytes\": %llu,\n",
            (unsigned long long)snap->mem_used_bytes);
    fprintf(f, "  \"mem_total_bytes\": %llu\n",
            (unsigned long long)snap->mem_total_bytes);
    fputs("}\n", f);
    fclose(f);
    return WT_OK;
}

static int wt_json_get_num(const char *buf, const char *key, double *out)
{
    char pat[64];
    if (FAILED(StringCchPrintfA(pat, sizeof(pat), "\"%s\"", key))) {
        return 0;
    }
    const char *p = strstr(buf, pat);
    if (p == NULL) {
        return 0;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') {
        ++p;
    }
    if (strncmp(p, "null", 4) == 0) {
        *out = -1.0;
        return 1;
    }
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) {
        return 0;
    }
    *out = v;
    return 1;
}

static int wt_json_get_u64(const char *buf, const char *key,
                           unsigned long long *out)
{
    double v = 0.0;
    if (!wt_json_get_num(buf, key, &v) || v < 0.0) {
        *out = 0;
        return 0;
    }
    *out = (unsigned long long)v;
    return 1;
}

static int wt_json_get_str(const char *buf, const char *key, char *out,
                           size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    char pat[64];
    if (FAILED(StringCchPrintfA(pat, sizeof(pat), "\"%s\"", key))) {
        return 0;
    }
    const char *p = strstr(buf, pat);
    if (p == NULL) {
        return 0;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t') {
        ++p;
    }
    if (*p != '"') {
        return 0;
    }
    ++p;
    size_t i = 0;
    while (*p != '\0' && *p != '"') {
        if (i + 1 < cap) {
            out[i++] = *p;
        }
        ++p;
    }
    out[i] = '\0';
    return 1;
}

WT_Result wt_tui_compare_load_slot(const wchar_t *slot, WT_TuiCompareSnap *out)
{
    if (!wt_tui_compare_slot_ok(slot) || out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_tui_compare_snap_clear(out);

    wchar_t dir[MAX_PATH];
    WT_Result r = wt_tui_compare_dir(dir, ARRAYSIZE(dir));
    if (r != WT_OK) {
        return r;
    }
    wchar_t path[MAX_PATH];
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\%s.json", dir,
                                slot))) {
        return WT_ERR_BUFFER_TOO_SMALL;
    }

    FILE *f = NULL;
    if (_wfopen_s(&f, path, L"rb") != 0 || f == NULL) {
        return WT_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0 || size > (1 << 16)) {
        fclose(f);
        return WT_ERR_WIN32;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);

    wt_json_get_str(buf, "captured_utc", out->captured_utc,
                    sizeof(out->captured_utc));
    wt_json_get_num(buf, "cpu_percent", &out->cpu_percent);
    wt_json_get_num(buf, "mem_percent", &out->mem_percent);
    wt_json_get_num(buf, "disk_percent", &out->disk_percent);
    wt_json_get_num(buf, "net_rx_bps", &out->net_rx_bps);
    wt_json_get_num(buf, "net_tx_bps", &out->net_tx_bps);
    wt_json_get_u64(buf, "mem_used_bytes", &out->mem_used_bytes);
    wt_json_get_u64(buf, "mem_total_bytes", &out->mem_total_bytes);
    out->valid = 1;
    free(buf);
    return WT_OK;
}

WT_Result wt_tui_compare_load_pair(WT_TuiComparePair *out)
{
    if (out == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    ZeroMemory(out, sizeof(*out));
    wt_tui_compare_snap_clear(&out->before);
    wt_tui_compare_snap_clear(&out->after);
    (void)wt_tui_compare_load_slot(L"before", &out->before);
    (void)wt_tui_compare_load_slot(L"after", &out->after);
    return WT_OK;
}

static void wt_tui_compare_fmt_pct(char *buf, size_t cap, double v)
{
    if (v < 0.0) {
        StringCchCopyA(buf, cap, "n/a");
    } else {
        StringCchPrintfA(buf, cap, "%.1f%%", v);
    }
}

static void wt_tui_compare_fmt_delta_pp(char *buf, size_t cap, double d,
                                        int have)
{
    if (!have) {
        StringCchCopyA(buf, cap, "n/a");
        return;
    }
    StringCchPrintfA(buf, cap, "%+.1f pp", d);
}

void wt_tui_compare_print(FILE *out, const WT_TuiComparePair *pair)
{
    if (out == NULL || pair == NULL) {
        return;
    }

    fputs("Before / After Compare\n", out);
    fputs("----------------------\n", out);
    if (!pair->before.valid && !pair->after.valid) {
        fputs("No snapshots yet. In the TUI: press b (before), apply a change,\n"
              "then a (after), then c to open this view.\n",
              out);
        fputs("Deltas are measured sample differences only - not lasting gains.\n",
              out);
        return;
    }

    char bcpu[16], acpu[16], bmem[16], amem[16], bdsk[16], adsk[16];
    wt_tui_compare_fmt_pct(bcpu, sizeof(bcpu), pair->before.cpu_percent);
    wt_tui_compare_fmt_pct(acpu, sizeof(acpu), pair->after.cpu_percent);
    wt_tui_compare_fmt_pct(bmem, sizeof(bmem), pair->before.mem_percent);
    wt_tui_compare_fmt_pct(amem, sizeof(amem), pair->after.mem_percent);
    wt_tui_compare_fmt_pct(bdsk, sizeof(bdsk), pair->before.disk_percent);
    wt_tui_compare_fmt_pct(adsk, sizeof(adsk), pair->after.disk_percent);

    fprintf(out, "            %-14s  %-14s\n", "Before", "After");
    fprintf(out, "  Captured  %-14s  %-14s\n",
            pair->before.valid ? pair->before.captured_utc : "(none)",
            pair->after.valid ? pair->after.captured_utc : "(none)");
    fprintf(out, "  CPU       %-14s  %-14s\n",
            pair->before.valid ? bcpu : "(none)",
            pair->after.valid ? acpu : "(none)");
    fprintf(out, "  Memory    %-14s  %-14s\n",
            pair->before.valid ? bmem : "(none)",
            pair->after.valid ? amem : "(none)");
    fprintf(out, "  Disk      %-14s  %-14s\n",
            pair->before.valid ? bdsk : "(none)",
            pair->after.valid ? adsk : "(none)");

    WT_TuiCompareDelta d;
    wt_tui_compare_delta(pair, &d);
    if (d.both_valid) {
        char dcpu[24], dmem[24], ddsk[24];
        wt_tui_compare_fmt_delta_pp(
            dcpu, sizeof(dcpu), d.cpu_pp,
            pair->before.cpu_percent >= 0.0 && pair->after.cpu_percent >= 0.0);
        wt_tui_compare_fmt_delta_pp(
            dmem, sizeof(dmem), d.mem_pp,
            pair->before.mem_percent >= 0.0 && pair->after.mem_percent >= 0.0);
        wt_tui_compare_fmt_delta_pp(
            ddsk, sizeof(ddsk), d.disk_pp,
            pair->before.disk_percent >= 0.0 && pair->after.disk_percent >= 0.0);
        fprintf(out, "\n  Delta (after - before):\n");
        fprintf(out, "  CPU       %s\n", dcpu);
        fprintf(out, "  Memory    %s\n", dmem);
        fprintf(out, "  Disk      %s\n", ddsk);
        if (pair->before.net_rx_bps >= 0.0 && pair->after.net_rx_bps >= 0.0) {
            fprintf(out, "  Net down  %+.0f B/s\n", d.net_rx_bps);
            fprintf(out, "  Net up    %+.0f B/s\n", d.net_tx_bps);
        }
        fputs("\n", out);
        fputs("Note: these are sample-window deltas only. They do not prove a\n"
              "lasting improvement. Re-measure after the system settles.\n",
              out);
    } else {
        fputs("\nCapture both before (b) and after (a) to see deltas.\n", out);
    }
}
