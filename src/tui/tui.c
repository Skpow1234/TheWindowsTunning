#include "tui/tui.h"
#include "tui/tui_screen.h"
#include "tui/tui_theme.h"
#include "tui/tui_widgets.h"
#include "tui/tui_input.h"
#include "tui/tui_compare.h"

#include "cli/cli.h"
#include "platform/console.h"
#include "platform/paths.h"
#include "common/units.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "metrics/network.h"
#include "metrics/gpu.h"
#include "system/os_info.h"
#include "system/power.h"
#include "system/services.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

#define WT_TUI_DEFAULT_INTERVAL_MS 1000
#define WT_TUI_POLL_STEP_MS 40
#define WT_TUI_TOP_PROC 64
#define WT_TUI_PROC_SAMPLE_MS 250
#define WT_TUI_HIST_LEN 24
#define WT_TUI_MIN_COLS 48
#define WT_TUI_MIN_ROWS 14
#define WT_TUI_MAX_WIDTH 140

typedef enum WT_TuiView {
    WT_VIEW_OVERVIEW = 0,
    WT_VIEW_DISK,
    WT_VIEW_MEMORY,
    WT_VIEW_NETWORK,
    WT_VIEW_GPU,
    WT_VIEW_POWER,
    WT_VIEW_SERVICES,
    WT_VIEW_COMPARE,
    WT_VIEW_HELP
} WT_TuiView;

typedef struct WT_TuiHistory {
    double cpu[WT_TUI_HIST_LEN];
    double mem[WT_TUI_HIST_LEN];
    double disk[WT_TUI_HIST_LEN];
    double net_rx[WT_TUI_HIST_LEN]; /* bytes/sec */
    double net_tx[WT_TUI_HIST_LEN]; /* bytes/sec */
    size_t count;
    size_t next;
} WT_TuiHistory;

typedef struct WT_TuiState {
    WT_TuiView view;
    WT_ProcessSort sort;
    size_t proc_scroll;
    size_t svc_scroll;
    int paused;
    int include_network; /* opt-in per-process TCP rates */
    int last_rows;
    int last_cols;
    char status[160];
    ULONGLONG status_until_ms;
    WT_TuiHistory hist;
    WT_TuiComparePair compare;
} WT_TuiState;

static volatile int g_tui_stop = 0;

static BOOL WINAPI wt_tui_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        g_tui_stop = 1;
        return TRUE;
    default:
        return FALSE;
    }
}

typedef struct WT_TuiPdh {
    PDH_HQUERY query;
    PDH_HCOUNTER cpu;
    PDH_HCOUNTER disk;
    int ok;
} WT_TuiPdh;

static void wt_tui_pdh_open(WT_TuiPdh *p)
{
    p->query = NULL;
    p->cpu = NULL;
    p->disk = NULL;
    p->ok = 0;

    if (PdhOpenQueryW(NULL, 0, &p->query) != ERROR_SUCCESS) {
        return;
    }
    PdhAddEnglishCounterW(p->query, L"\\Processor(_Total)\\% Processor Time",
                          0, &p->cpu);
    PdhAddEnglishCounterW(p->query, L"\\PhysicalDisk(_Total)\\% Disk Time",
                          0, &p->disk);
    PdhCollectQueryData(p->query);
    p->ok = 1;
}

static double wt_tui_pdh_value(PDH_HCOUNTER counter)
{
    if (counter == NULL) {
        return -1.0;
    }
    PDH_FMT_COUNTERVALUE value;
    DWORD type = 0;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &type, &value)
            == ERROR_SUCCESS && value.CStatus == PDH_CSTATUS_VALID_DATA) {
        return value.doubleValue;
    }
    return -1.0;
}

static void wt_tui_pdh_read(WT_TuiPdh *p, double *cpu, double *disk)
{
    *cpu = -1.0;
    *disk = -1.0;
    if (!p->ok) {
        return;
    }
    if (PdhCollectQueryData(p->query) != ERROR_SUCCESS) {
        return;
    }
    *cpu = wt_tui_pdh_value(p->cpu);
    *disk = wt_tui_pdh_value(p->disk);
}

static void wt_tui_pdh_close(WT_TuiPdh *p)
{
    if (p->query != NULL) {
        PdhCloseQuery(p->query);
        p->query = NULL;
    }
    p->ok = 0;
}

static void wt_tui_human(unsigned long long bytes, char *out, size_t cap)
{
    wchar_t w[32];
    if (wt_format_bytes(bytes, w, 32) != WT_OK) {
        w[0] = L'\0';
    }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
}

static void wt_tui_rate(double bytes_per_sec, char *out, size_t cap)
{
    const char *unit = "B/s";
    double v = bytes_per_sec;
    if (v >= 1024.0 * 1024.0) {
        v /= 1024.0 * 1024.0;
        unit = "MB/s";
    } else if (v >= 1024.0) {
        v /= 1024.0;
        unit = "KB/s";
    }
    snprintf(out, cap, "%.1f %s", v, unit);
}

static void wt_tui_hist_push(WT_TuiHistory *h, double cpu, double mem,
                             double disk, double net_rx, double net_tx)
{
    if (h == NULL) {
        return;
    }
    h->cpu[h->next] = cpu < 0.0 ? 0.0 : cpu;
    h->mem[h->next] = mem < 0.0 ? 0.0 : mem;
    h->disk[h->next] = disk < 0.0 ? 0.0 : disk;
    h->net_rx[h->next] = net_rx < 0.0 ? 0.0 : net_rx;
    h->net_tx[h->next] = net_tx < 0.0 ? 0.0 : net_tx;
    h->next = (h->next + 1) % WT_TUI_HIST_LEN;
    if (h->count < WT_TUI_HIST_LEN) {
        h->count++;
    }
}

static void wt_tui_hist_copy(const WT_TuiHistory *h, const double *src,
                             double *out, size_t *out_count)
{
    size_t n = h->count;
    *out_count = n;
    if (n == 0) {
        return;
    }
    size_t start = (h->next + WT_TUI_HIST_LEN - n) % WT_TUI_HIST_LEN;
    for (size_t i = 0; i < n; ++i) {
        out[i] = src[(start + i) % WT_TUI_HIST_LEN];
    }
}

/* Scales absolute samples into 0..100 relative to the window max (for net). */
static void wt_tui_hist_normalize(const double *src, size_t n, double *out)
{
    double max = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (src[i] > max) {
            max = src[i];
        }
    }
    if (max <= 0.0) {
        for (size_t i = 0; i < n; ++i) {
            out[i] = 0.0;
        }
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = (src[i] / max) * 100.0;
    }
}

static void wt_tui_set_status(WT_TuiState *st, const char *msg, unsigned hold_ms)
{
    if (st == NULL) {
        return;
    }
    if (msg == NULL) {
        st->status[0] = '\0';
        st->status_until_ms = 0;
        return;
    }
    StringCchCopyA(st->status, ARRAYSIZE(st->status), msg);
    st->status_until_ms = GetTickCount64() + hold_ms;
}

static const char *wt_tui_sort_name(WT_ProcessSort sort)
{
    switch (sort) {
    case WT_PROCESS_SORT_CPU: return "cpu";
    case WT_PROCESS_SORT_DISK: return "disk";
    case WT_PROCESS_SORT_NETWORK: return "net";
    default: return "memory";
    }
}

static void wt_tui_render_header(WT_TuiScreen *s, const WT_TuiTheme *t, int width,
                                 const WT_OsInfo *os, int os_ok,
                                 const WT_TuiState *st)
{
    (void)width;
    char host[80] = "unknown";
    char uptime[40] = "?";
    if (os_ok) {
        WideCharToMultiByte(CP_UTF8, 0, os->hostname, -1, host, sizeof(host), NULL, NULL);
        wchar_t up[32];
        if (wt_format_duration_ms(os->uptime_ms, up, 32) == WT_OK) {
            WideCharToMultiByte(CP_UTF8, 0, up, -1, uptime, sizeof(uptime), NULL, NULL);
        }
    }

    WT_PowerInfo power;
    char power_str[64] = "unknown";
    if (wt_collect_power_info(&power) == WT_OK) {
        const char *src = power.on_ac == 1 ? " (AC)"
                        : power.on_ac == 0 ? " (battery)" : "";
        snprintf(power_str, sizeof(power_str), "%s%s",
                 wt_power_scheme_name(power.scheme), src);
    }

    const char *pause = (st != NULL && st->paused) ? "  [PAUSED]" : "";
    wt_tui_screen_line(s, "%s%sWinTune Live%s%s   Host: %s   Up: %s   Power: %s",
                       wt_tui_bold(t), wt_tui_cyan(t), wt_tui_reset(t), pause,
                       host, uptime, power_str);
    wt_tui_rule_line(s, t, width);
}

static void wt_tui_render_gauges(WT_TuiScreen *s, const WT_TuiTheme *t,
                                 double cpu, double mem_pct, double disk,
                                 const WT_MemoryMetrics *mem, int mem_ok,
                                 double rx, double tx, int net_ok,
                                 const WT_TuiHistory *hist)
{
    char mem_suffix[64] = "";
    if (mem_ok) {
        char used[32], total[32];
        wt_tui_human(mem->used_physical_bytes, used, sizeof(used));
        wt_tui_human(mem->total_physical_bytes, total, sizeof(total));
        snprintf(mem_suffix, sizeof(mem_suffix), "%s / %s", used, total);
    }

    int bar_w = t->gauge_width;
    wt_tui_gauge_line(s, t, "CPU", cpu < 0 ? 0.0 : cpu, bar_w,
                      cpu < 0 ? "(sampling)" : "");
    wt_tui_gauge_line(s, t, "RAM", mem_ok ? mem_pct : 0.0, bar_w, mem_suffix);
    wt_tui_gauge_line(s, t, "DISK", disk < 0 ? 0.0 : disk, bar_w,
                      disk < 0 ? "(sampling)" : "active");

    if (hist != NULL && hist->count > 1 && t->preset != WT_TUI_THEME_COMPACT) {
        double cpu_s[WT_TUI_HIST_LEN], mem_s[WT_TUI_HIST_LEN], disk_s[WT_TUI_HIST_LEN];
        size_t n = 0;
        wt_tui_hist_copy(hist, hist->cpu, cpu_s, &n);
        wt_tui_sparkline_line(s, t, "cpu~", cpu_s, n);
        wt_tui_hist_copy(hist, hist->mem, mem_s, &n);
        wt_tui_sparkline_line(s, t, "ram~", mem_s, n);
        wt_tui_hist_copy(hist, hist->disk, disk_s, &n);
        wt_tui_sparkline_line(s, t, "dsk~", disk_s, n);
    }

    char rxs[24] = "n/a", txs[24] = "n/a";
    if (net_ok) {
        wt_tui_rate(rx, rxs, sizeof(rxs));
        wt_tui_rate(tx, txs, sizeof(txs));
    }
    wt_tui_screen_line(s, "NET   down %-12s  up %-12s", rxs, txs);

    if (hist != NULL && hist->count > 1 && t->preset != WT_TUI_THEME_COMPACT) {
        double rx_s[WT_TUI_HIST_LEN], tx_s[WT_TUI_HIST_LEN];
        double rx_n[WT_TUI_HIST_LEN], tx_n[WT_TUI_HIST_LEN];
        size_t n = 0;
        wt_tui_hist_copy(hist, hist->net_rx, rx_s, &n);
        wt_tui_hist_normalize(rx_s, n, rx_n);
        wt_tui_sparkline_line(s, t, "dn~", rx_n, n);
        wt_tui_hist_copy(hist, hist->net_tx, tx_s, &n);
        wt_tui_hist_normalize(tx_s, n, tx_n);
        wt_tui_sparkline_line(s, t, "up~", tx_n, n);
    }
}

static void wt_tui_render_processes(WT_TuiScreen *s, const WT_TuiTheme *t,
                                    const WT_ProcessInfo *procs, size_t count,
                                    size_t scroll, int max_rows,
                                    WT_ProcessSort sort)
{
    if (count == 0) {
        wt_tui_empty_line(s, t, "(no process data — press r to refresh)");
        return;
    }

    int show_cpu = 0;
    int show_net = 0;
    for (size_t i = 0; i < count; ++i) {
        if (procs[i].cpu_percent >= 0.0) {
            show_cpu = 1;
        }
        if (procs[i].net_recv_bytes_per_sec >= 0.0 ||
            procs[i].net_send_bytes_per_sec >= 0.0) {
            show_net = 1;
        }
    }

    wt_tui_screen_line(s, "%ssort:%s %-6s  %sscroll:%s %zu/%zu%s",
                       wt_tui_dim(t), wt_tui_reset(t), wt_tui_sort_name(sort),
                       wt_tui_dim(t), wt_tui_reset(t),
                       scroll + 1 > count ? count : scroll + 1, count,
                       show_net ? "  net:on" : "");

    if (show_cpu && show_net) {
        wt_tui_screen_line(s, "%s%-6s %-16s %6s %9s %8s %8s%s",
                           wt_tui_dim(t), "PID", "Process", "CPU%", "Memory",
                           "Disk", "Net", wt_tui_reset(t));
    } else if (show_cpu) {
        wt_tui_screen_line(s, "%s%-6s %-20s %6s %10s %10s%s",
                           wt_tui_dim(t), "PID", "Process", "CPU%", "Memory",
                           "Disk", wt_tui_reset(t));
    } else {
        wt_tui_screen_line(s, "%s%-6s %-26s %12s %12s%s",
                           wt_tui_dim(t), "PID", "Process", "Memory", "Private",
                           wt_tui_reset(t));
    }

    if (scroll >= count) {
        scroll = count > 0 ? count - 1 : 0;
    }

    int shown = 0;
    for (size_t i = scroll; i < count && shown < max_rows; ++i, ++shown) {
        char name[64];
        WideCharToMultiByte(CP_UTF8, 0, procs[i].name, -1, name, sizeof(name),
                            NULL, NULL);
        char ws[32], pv[32];
        wt_tui_human(procs[i].working_set_bytes, ws, sizeof(ws));
        wt_tui_human(procs[i].private_bytes, pv, sizeof(pv));

        if (show_cpu && show_net) {
            double disk_rate = 0.0;
            if (procs[i].disk_read_bytes_per_sec >= 0.0) {
                disk_rate += procs[i].disk_read_bytes_per_sec;
            }
            if (procs[i].disk_write_bytes_per_sec >= 0.0) {
                disk_rate += procs[i].disk_write_bytes_per_sec;
            }
            double net_rate = 0.0;
            if (procs[i].net_recv_bytes_per_sec >= 0.0) {
                net_rate += procs[i].net_recv_bytes_per_sec;
            }
            if (procs[i].net_send_bytes_per_sec >= 0.0) {
                net_rate += procs[i].net_send_bytes_per_sec;
            }
            char disk[32], net[32];
            wt_tui_rate(disk_rate, disk, sizeof(disk));
            wt_tui_rate(net_rate, net, sizeof(net));
            if (procs[i].cpu_percent >= 0.0) {
                wt_tui_screen_line(s, "%-6lu %-16.16s %5.1f%% %9s %8s %8s",
                                   procs[i].pid, name, procs[i].cpu_percent, ws,
                                   disk, net);
            } else {
                wt_tui_screen_line(s, "%-6lu %-16.16s %6s %9s %8s %8s",
                                   procs[i].pid, name, "-", ws, disk, net);
            }
        } else if (show_cpu) {
            double disk_rate = 0.0;
            if (procs[i].disk_read_bytes_per_sec >= 0.0) {
                disk_rate += procs[i].disk_read_bytes_per_sec;
            }
            if (procs[i].disk_write_bytes_per_sec >= 0.0) {
                disk_rate += procs[i].disk_write_bytes_per_sec;
            }
            char disk[32];
            wt_tui_rate(disk_rate, disk, sizeof(disk));
            if (procs[i].cpu_percent >= 0.0) {
                wt_tui_screen_line(s, "%-6lu %-20.20s %5.1f%% %10s %10s",
                                   procs[i].pid, name, procs[i].cpu_percent, ws,
                                   disk);
            } else {
                wt_tui_screen_line(s, "%-6lu %-20.20s %6s %10s %10s",
                                   procs[i].pid, name, "-", ws, disk);
            }
        } else {
            wt_tui_screen_line(s, "%-6lu %-26.26s %12s %12s",
                               procs[i].pid, name, ws, pv);
        }
    }

    if (scroll + (size_t)shown < count) {
        wt_tui_empty_line(s, t, "... more (j/k or arrows to scroll)");
    }
}

static void wt_tui_render_disk(WT_TuiScreen *s, const WT_TuiTheme *t, double active)
{
    WT_DiskVolumeMetrics vols[32];
    size_t n = 0;
    wt_tui_screen_line(s, "%s%-8s %16s %16s %10s%s",
                       wt_tui_dim(t), "Volume", "Free", "Total", "Free %",
                       wt_tui_reset(t));
    if (wt_collect_disk_volumes(vols, 32, &n) == WT_OK && n > 0) {
        for (size_t i = 0; i < n; ++i) {
            char root[16], freeb[32], totalb[32];
            WideCharToMultiByte(CP_UTF8, 0, vols[i].root_path, -1, root,
                                sizeof(root), NULL, NULL);
            wt_tui_human(vols[i].free_bytes, freeb, sizeof(freeb));
            wt_tui_human(vols[i].total_bytes, totalb, sizeof(totalb));
            wt_tui_screen_line(s, "%-8s %16s %16s %9.1f%%",
                               root, freeb, totalb, vols[i].free_percent);
        }
    } else {
        wt_tui_empty_line(s, t, "(disk volume metrics unavailable)");
    }
    wt_tui_screen_line(s, "");
    if (active < 0.0) {
        wt_tui_empty_line(s, t, "Disk active time: (sampling)");
    } else {
        wt_tui_screen_line(s, "Disk active time: %s%.0f%%%s",
                           wt_tui_color_for_pct(t, active), active,
                           wt_tui_reset(t));
    }
}

static void wt_tui_render_memory(WT_TuiScreen *s, const WT_TuiTheme *t,
                                 const WT_MemoryMetrics *mem, int mem_ok,
                                 const WT_ProcessInfo *procs, size_t count,
                                 size_t scroll, int max_rows,
                                 WT_ProcessSort sort)
{
    if (mem_ok) {
        char total[32], used[32], avail[32];
        wt_tui_human(mem->total_physical_bytes, total, sizeof(total));
        wt_tui_human(mem->used_physical_bytes, used, sizeof(used));
        wt_tui_human(mem->available_physical_bytes, avail, sizeof(avail));
        wt_tui_screen_line(s, "Total: %-12s  Used: %-12s  Available: %-12s  (%.1f%% used)",
                           total, used, avail, mem->used_percent);
    } else {
        wt_tui_empty_line(s, t, "(memory metrics unavailable)");
    }
    wt_tui_screen_line(s, "");
    wt_tui_render_processes(s, t, procs, count, scroll, max_rows, sort);
}

static void wt_tui_render_network(WT_TuiScreen *s, const WT_TuiTheme *t,
                                  const WT_NetTotals *now, int net_ok,
                                  double rx, double tx)
{
    if (!net_ok) {
        wt_tui_empty_line(s, t, "(network metrics unavailable)");
        return;
    }
    char rxs[24], txs[24], inb[32], outb[32];
    wt_tui_rate(rx, rxs, sizeof(rxs));
    wt_tui_rate(tx, txs, sizeof(txs));
    wt_tui_human(now->in_bytes, inb, sizeof(inb));
    wt_tui_human(now->out_bytes, outb, sizeof(outb));
    wt_tui_screen_line(s, "Active interfaces: %u", now->interface_count);
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "Throughput   down %-14s  up %-14s", rxs, txs);
    wt_tui_screen_line(s, "Cumulative   in   %-14s  out %-14s", inb, outb);
    wt_tui_screen_line(s, "");
    wt_tui_empty_line(s, t,
                      "Per-process TCP rates: press w (or --include-network).");
    wt_tui_empty_line(s, t,
                      "Uses IP Helper Extended Stats; no payload capture.");
}

static void wt_tui_render_gpu(WT_TuiScreen *s, const WT_TuiTheme *t)
{
    WT_GpuMetrics gpu;
    if (wt_collect_gpu_metrics(250, &gpu) != WT_OK) {
        wt_tui_empty_line(s, t, "(GPU / display metrics unavailable)");
        return;
    }

    if (gpu.display.available) {
        wt_tui_screen_line(s, "Displays: %u active   primary %ux%u",
                           gpu.display.display_count, gpu.display.primary_width,
                           gpu.display.primary_height);
        wt_tui_screen_line(s, "");
    }

    if (!gpu.adapters_ok || gpu.adapter_count == 0) {
        wt_tui_empty_line(s, t, "(no hardware GPU adapters found)");
        return;
    }

    wt_tui_screen_line(s, "%s%-36s %10s %8s%s", wt_tui_dim(t), "Adapter",
                       "Dedicated", "Busy", wt_tui_reset(t));
    for (size_t i = 0; i < gpu.adapter_count; ++i) {
        char name[96], ded[32];
        WideCharToMultiByte(CP_UTF8, 0, gpu.adapters[i].name, -1, name,
                            sizeof(name), NULL, NULL);
        wt_tui_human(gpu.adapters[i].dedicated_bytes, ded, sizeof(ded));
        if (gpu.adapters[i].utilization_ok) {
            wt_tui_screen_line(s, "%-36.36s %10s %6.0f%%", name, ded,
                               gpu.adapters[i].utilization_percent);
        } else {
            wt_tui_screen_line(s, "%-36.36s %10s %8s", name, ded, "n/a");
        }
    }
    wt_tui_screen_line(s, "");
    wt_tui_empty_line(s, t, "Busy = max GPU Engine utilization (PDH). Read-only.");
    wt_tui_empty_line(s, t, "WinTune never changes drivers, clocks, or FPS settings.");
}

static void wt_tui_render_power(WT_TuiScreen *s, const WT_TuiTheme *t)
{
    WT_PowerInfo p;
    if (wt_collect_power_info(&p) != WT_OK) {
        wt_tui_empty_line(s, t, "(power information unavailable)");
        return;
    }
    char plan_name[128];
    WideCharToMultiByte(CP_UTF8, 0, p.active_name, -1, plan_name,
                        sizeof(plan_name), NULL, NULL);
    wt_tui_screen_line(s, "Active plan : %s (%s)", plan_name,
                       wt_power_scheme_name(p.scheme));
    wt_tui_screen_line(s, "Power source: %s",
                       p.on_ac == 1 ? "AC (plugged in)"
                       : p.on_ac == 0 ? "Battery" : "unknown");
    if (p.battery_percent >= 0) {
        wt_tui_screen_line(s, "Battery     : %d%%%s%s", p.battery_percent,
                           p.charging == 1 ? " charging" : "",
                           p.discharging == 1 ? " discharging" : "");
    }
    if (p.rate_ok && p.on_ac == 0 && p.discharging == 1 && p.rate_mw < 0) {
        wt_tui_screen_line(s, "Discharge   : %.1f W%s",
                           (-(double)p.rate_mw) / 1000.0,
                           p.estimated_seconds > 0 ? "" : "");
        if (p.estimated_seconds > 0) {
            wt_tui_screen_line(s, "Est. remain : ~%d min",
                               p.estimated_seconds / 60);
        }
    }
    if (p.processor_max_pct_ac >= 0 || p.processor_max_pct_dc >= 0) {
        char acbuf[16], dcbuf[16];
        if (p.processor_max_pct_ac >= 0) {
            snprintf(acbuf, sizeof(acbuf), "%d%%", p.processor_max_pct_ac);
        } else {
            snprintf(acbuf, sizeof(acbuf), "n/a");
        }
        if (p.processor_max_pct_dc >= 0) {
            snprintf(dcbuf, sizeof(dcbuf), "%d%%", p.processor_max_pct_dc);
        } else {
            snprintf(dcbuf, sizeof(dcbuf), "n/a");
        }
        wt_tui_screen_line(s, "CPU max AC/DC: %s / %s%s", acbuf, dcbuf,
                           p.processor_capped ? " (capped)" : "");
    }
    wt_tui_screen_line(s, "");
    wt_tui_empty_line(s, t,
                      "Use 'wintune power --set <plan>' to change the plan.");
    wt_tui_empty_line(s, t,
                      "Read-only budget hints — no fan or firmware changes.");
}

static void wt_tui_render_services(WT_TuiScreen *s, const WT_TuiTheme *t,
                                   const WT_ServiceInfo *svcs, size_t count,
                                   int loaded, size_t scroll, int max_rows)
{
    if (!loaded) {
        wt_tui_empty_line(s, t, "(collecting services...)");
        return;
    }
    if (count == 0) {
        wt_tui_empty_line(s, t, "(no services found — access denied?)");
        return;
    }

    size_t running = 0, stopped = 0, autostart = 0;
    size_t run_idx[WT_MAX_SERVICES];
    size_t run_n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (svcs[i].state == WT_SVC_STATE_RUNNING) {
            running++;
            if (run_n < WT_MAX_SERVICES) {
                run_idx[run_n++] = i;
            }
        }
        if (svcs[i].state == WT_SVC_STATE_STOPPED) stopped++;
        if (svcs[i].start_type == WT_SVC_START_AUTO) autostart++;
    }
    wt_tui_screen_line(s, "Total: %zu   running: %zu   stopped: %zu   auto-start: %zu",
                       count, running, stopped, autostart);
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "%s%-34s %-9s %-9s %-7s%s",
                       wt_tui_dim(t), "Name", "State", "Start", "PID",
                       wt_tui_reset(t));

    if (run_n == 0) {
        wt_tui_empty_line(s, t, "(no running services in list)");
        return;
    }
    if (scroll >= run_n) {
        scroll = run_n - 1;
    }

    int shown = 0;
    for (size_t i = scroll; i < run_n && shown < max_rows; ++i, ++shown) {
        const WT_ServiceInfo *svc = &svcs[run_idx[i]];
        char name[48];
        WideCharToMultiByte(CP_UTF8, 0, svc->name, -1, name, sizeof(name),
                            NULL, NULL);
        if (svc->pid == 0) {
            wt_tui_screen_line(s, "%-34.34s %-9s %-9s %-7s", name,
                               wt_service_state_name(svc->state),
                               wt_service_start_type_name(svc->start_type), "-");
        } else {
            wt_tui_screen_line(s, "%-34.34s %-9s %-9s %-7lu", name,
                               wt_service_state_name(svc->state),
                               wt_service_start_type_name(svc->start_type),
                               svc->pid);
        }
    }
    if (scroll + (size_t)shown < run_n) {
        wt_tui_empty_line(s, t, "... more (j/k or arrows to scroll)");
    }
}

static void wt_tui_render_help(WT_TuiScreen *s, const WT_TuiTheme *t)
{
    wt_tui_screen_line(s, "%sKeyboard%s", wt_tui_bold(t), wt_tui_reset(t));
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "  o     Overview (CPU/RAM/disk/net + top processes)");
    wt_tui_screen_line(s, "  d/m/n/g Disk / Memory / Network / GPU views");
    wt_tui_screen_line(s, "  p/s   Power / Services views");
    wt_tui_screen_line(s, "  t     Cycle process sort (cpu → memory → disk → net)");
    wt_tui_screen_line(s, "  1/2/3/4  Sort by CPU / Memory / Disk / Network");
    wt_tui_screen_line(s, "  w     Toggle per-process TCP net columns (extra overhead)");
    wt_tui_screen_line(s, "  j/k   Scroll process/service list (arrows too)");
    wt_tui_screen_line(s, "  PgUp/PgDn  Page scroll");
    wt_tui_screen_line(s, "  Space Pause / resume refresh");
    wt_tui_screen_line(s, "  e     Export snapshot to Documents\\WinTune\\Reports");
    wt_tui_screen_line(s, "  b/a   Mark before / after snapshot (for compare)");
    wt_tui_screen_line(s, "  c     Before/after compare view (measured deltas only)");
    wt_tui_screen_line(s, "  r     Refresh now");
    wt_tui_screen_line(s, "  ?/h   Toggle this help");
    wt_tui_screen_line(s, "  q     Quit (Esc / Ctrl+C also quit)");
    wt_tui_screen_line(s, "");
    wt_tui_empty_line(s, t, "Minimum size: 48x14. Themes: --theme default|compact|mono");
    wt_tui_empty_line(s, t, "Net columns: TCP Extended Stats only; UDP not included.");
    wt_tui_empty_line(s, t, "Compare deltas are sample differences — not lasting gains.");
    wt_tui_empty_line(s, t, "WinTune never changes the system from the dashboard.");
}

static void wt_tui_render_compare(WT_TuiScreen *s, const WT_TuiTheme *t,
                                  const WT_TuiComparePair *pair)
{
    char bcpu[16] = "(none)", acpu[16] = "(none)";
    char bmem[16] = "(none)", amem[16] = "(none)";
    char bdsk[16] = "(none)", adsk[16] = "(none)";

    if (pair == NULL || (!pair->before.valid && !pair->after.valid)) {
        wt_tui_empty_line(s, t,
                          "No snapshots yet. Press b (before), then a (after).");
        wt_tui_empty_line(s, t,
                          "Deltas are sample differences only - not lasting gains.");
        return;
    }

    wt_tui_screen_line(s, "            %-14s  %-14s", "Before", "After");
    wt_tui_screen_line(s, "  Captured  %-14.14s  %-14.14s",
                       pair->before.valid ? pair->before.captured_utc : "(none)",
                       pair->after.valid ? pair->after.captured_utc : "(none)");

    if (pair->before.valid) {
        if (pair->before.cpu_percent >= 0.0)
            StringCchPrintfA(bcpu, sizeof(bcpu), "%.1f%%",
                             pair->before.cpu_percent);
        else
            StringCchCopyA(bcpu, sizeof(bcpu), "n/a");
        if (pair->before.mem_percent >= 0.0)
            StringCchPrintfA(bmem, sizeof(bmem), "%.1f%%",
                             pair->before.mem_percent);
        else
            StringCchCopyA(bmem, sizeof(bmem), "n/a");
        if (pair->before.disk_percent >= 0.0)
            StringCchPrintfA(bdsk, sizeof(bdsk), "%.1f%%",
                             pair->before.disk_percent);
        else
            StringCchCopyA(bdsk, sizeof(bdsk), "n/a");
    }
    if (pair->after.valid) {
        if (pair->after.cpu_percent >= 0.0)
            StringCchPrintfA(acpu, sizeof(acpu), "%.1f%%",
                             pair->after.cpu_percent);
        else
            StringCchCopyA(acpu, sizeof(acpu), "n/a");
        if (pair->after.mem_percent >= 0.0)
            StringCchPrintfA(amem, sizeof(amem), "%.1f%%",
                             pair->after.mem_percent);
        else
            StringCchCopyA(amem, sizeof(amem), "n/a");
        if (pair->after.disk_percent >= 0.0)
            StringCchPrintfA(adsk, sizeof(adsk), "%.1f%%",
                             pair->after.disk_percent);
        else
            StringCchCopyA(adsk, sizeof(adsk), "n/a");
    }
    wt_tui_screen_line(s, "  CPU       %-14s  %-14s", bcpu, acpu);
    wt_tui_screen_line(s, "  Memory    %-14s  %-14s", bmem, amem);
    wt_tui_screen_line(s, "  Disk      %-14s  %-14s", bdsk, adsk);

    WT_TuiCompareDelta d;
    wt_tui_compare_delta(pair, &d);
    if (d.both_valid) {
        wt_tui_screen_line(s, "");
        wt_tui_screen_line(s, "  Delta (after - before):");
        if (pair->before.cpu_percent >= 0.0 && pair->after.cpu_percent >= 0.0) {
            wt_tui_screen_line(s, "  CPU       %+.1f pp", d.cpu_pp);
        }
        if (pair->before.mem_percent >= 0.0 && pair->after.mem_percent >= 0.0) {
            wt_tui_screen_line(s, "  Memory    %+.1f pp", d.mem_pp);
        }
        if (pair->before.disk_percent >= 0.0 && pair->after.disk_percent >= 0.0) {
            wt_tui_screen_line(s, "  Disk      %+.1f pp", d.disk_pp);
        }
        if (pair->before.net_rx_bps >= 0.0 && pair->after.net_rx_bps >= 0.0) {
            wt_tui_screen_line(s, "  Net down  %+.0f B/s", d.net_rx_bps);
            wt_tui_screen_line(s, "  Net up    %+.0f B/s", d.net_tx_bps);
        }
        wt_tui_screen_line(s, "");
        wt_tui_empty_line(s, t,
                          "Sample-window deltas only - not lasting gains.");
    } else {
        wt_tui_screen_line(s, "");
        wt_tui_empty_line(s, t, "Capture both b and a to see deltas.");
    }
}

static const char *wt_tui_view_title(WT_TuiView view)
{
    switch (view) {
    case WT_VIEW_DISK:     return "Disk";
    case WT_VIEW_MEMORY:   return "Memory";
    case WT_VIEW_NETWORK:  return "Network";
    case WT_VIEW_GPU:      return "GPU";
    case WT_VIEW_POWER:    return "Power";
    case WT_VIEW_SERVICES: return "Services";
    case WT_VIEW_COMPARE:  return "Before / After";
    case WT_VIEW_HELP:     return "Help";
    default:               return "Top Processes";
    }
}

static void wt_tui_clamp_scroll(size_t *scroll, size_t count, int page)
{
    if (count == 0) {
        *scroll = 0;
        return;
    }
    if (*scroll >= count) {
        *scroll = count - 1;
    }
    if (page > 0 && *scroll + (size_t)page > count) {
        /* keep scroll valid for visible window */
        size_t max_start = count > (size_t)page ? count - (size_t)page : 0;
        if (*scroll > max_start) {
            *scroll = max_start;
        }
    }
}

static int wt_tui_export_snapshot(const WT_TuiState *st,
                                  double cpu, double disk,
                                  const WT_MemoryMetrics *mem, int mem_ok,
                                  double rx, double tx, int net_ok,
                                  const WT_ProcessInfo *procs, size_t proc_count,
                                  char *msg, size_t msg_cap)
{
    wchar_t dir[MAX_PATH];
    if (wt_paths_reports_dir(dir, ARRAYSIZE(dir)) != WT_OK) {
        StringCchCopyA(msg, msg_cap, "export failed: reports path");
        return 0;
    }
    (void)wt_paths_ensure_dir(dir);

    SYSTEMTIME stime;
    GetLocalTime(&stime);
    wchar_t stem[MAX_PATH];
    if (FAILED(StringCchPrintfW(
            stem, ARRAYSIZE(stem),
            L"%s\\wintune-tui-%04u%02u%02u-%02u%02u%02u", dir,
            stime.wYear, stime.wMonth, stime.wDay, stime.wHour, stime.wMinute,
            stime.wSecond))) {
        StringCchCopyA(msg, msg_cap, "export failed: path too long");
        return 0;
    }

    double cpu_h[WT_TUI_HIST_LEN], mem_h[WT_TUI_HIST_LEN], disk_h[WT_TUI_HIST_LEN];
    double rx_h[WT_TUI_HIST_LEN], tx_h[WT_TUI_HIST_LEN];
    size_t hist_n = 0;
    wt_tui_hist_copy(&st->hist, st->hist.cpu, cpu_h, &hist_n);
    wt_tui_hist_copy(&st->hist, st->hist.mem, mem_h, &hist_n);
    wt_tui_hist_copy(&st->hist, st->hist.disk, disk_h, &hist_n);
    wt_tui_hist_copy(&st->hist, st->hist.net_rx, rx_h, &hist_n);
    wt_tui_hist_copy(&st->hist, st->hist.net_tx, tx_h, &hist_n);

    wchar_t path_txt[MAX_PATH], path_json[MAX_PATH], path_csv[MAX_PATH];
    StringCchPrintfW(path_txt, ARRAYSIZE(path_txt), L"%s.txt", stem);
    StringCchPrintfW(path_json, ARRAYSIZE(path_json), L"%s.json", stem);
    StringCchPrintfW(path_csv, ARRAYSIZE(path_csv), L"%s.csv", stem);

    FILE *f = NULL;
    if (_wfopen_s(&f, path_txt, L"wb") != 0 || f == NULL) {
        StringCchCopyA(msg, msg_cap, "export failed: open .txt");
        return 0;
    }

    fprintf(f, "WinTune TUI snapshot\n");
    fprintf(f, "sort=%s paused=%s history_samples=%zu\n",
            wt_tui_sort_name(st->sort), st->paused ? "yes" : "no", hist_n);
    fprintf(f, "CPU: %s%.1f%%\n", cpu < 0 ? "n/a " : "", cpu < 0 ? 0.0 : cpu);
    if (mem_ok) {
        fprintf(f, "Memory: %.1f%% used\n", mem->used_percent);
    }
    fprintf(f, "Disk active: %s%.0f%%\n", disk < 0 ? "n/a " : "",
            disk < 0 ? 0.0 : disk);
    if (net_ok) {
        char rxs[24], txs[24];
        wt_tui_rate(rx, rxs, sizeof(rxs));
        wt_tui_rate(tx, txs, sizeof(txs));
        fprintf(f, "Net: down %s  up %s\n", rxs, txs);
    }
    if (hist_n > 0) {
        fprintf(f, "\nHistory (oldest -> newest):\n");
        fprintf(f, "idx  cpu%%   mem%%   disk%%     rx_Bps     tx_Bps\n");
        for (size_t i = 0; i < hist_n; ++i) {
            fprintf(f, "%3zu %6.1f %6.1f %7.1f %10.0f %10.0f\n", i, cpu_h[i],
                    mem_h[i], disk_h[i], rx_h[i], tx_h[i]);
        }
    }
    fprintf(f, "\nPID      Process                   CPU%%     Memory      Disk\n");
    for (size_t i = 0; i < proc_count; ++i) {
        char name[64];
        WideCharToMultiByte(CP_UTF8, 0, procs[i].name, -1, name, sizeof(name),
                            NULL, NULL);
        char ws[32];
        wt_tui_human(procs[i].working_set_bytes, ws, sizeof(ws));
        double disk_rate = 0.0;
        if (procs[i].disk_read_bytes_per_sec >= 0.0) {
            disk_rate += procs[i].disk_read_bytes_per_sec;
        }
        if (procs[i].disk_write_bytes_per_sec >= 0.0) {
            disk_rate += procs[i].disk_write_bytes_per_sec;
        }
        char dr[32];
        wt_tui_rate(disk_rate, dr, sizeof(dr));
        if (procs[i].cpu_percent >= 0.0) {
            fprintf(f, "%-8lu %-24.24s %6.1f  %-10s %-10s\n",
                    procs[i].pid, name, procs[i].cpu_percent, ws, dr);
        } else {
            fprintf(f, "%-8lu %-24.24s %6s  %-10s %-10s\n",
                    procs[i].pid, name, "-", ws, dr);
        }
    }
    fclose(f);

    /* CSV history */
    if (_wfopen_s(&f, path_csv, L"wb") == 0 && f != NULL) {
        fputs("index,cpu_percent,mem_percent,disk_percent,net_rx_bps,net_tx_bps\n",
              f);
        for (size_t i = 0; i < hist_n; ++i) {
            fprintf(f, "%zu,%.3f,%.3f,%.3f,%.3f,%.3f\n", i, cpu_h[i], mem_h[i],
                    disk_h[i], rx_h[i], tx_h[i]);
        }
        fclose(f);
    }

    /* JSON snapshot + history */
    if (_wfopen_s(&f, path_json, L"wb") == 0 && f != NULL) {
        fputs("{\n", f);
        fputs("  \"command\": \"tui\",\n", f);
        fprintf(f, "  \"sort\": \"%s\",\n", wt_tui_sort_name(st->sort));
        fprintf(f, "  \"paused\": %s,\n", st->paused ? "true" : "false");
        fputs("  \"current\": {\n", f);
        if (cpu >= 0.0) {
            fprintf(f, "    \"cpu_percent\": %.2f,\n", cpu);
        } else {
            fputs("    \"cpu_percent\": null,\n", f);
        }
        if (mem_ok) {
            fprintf(f, "    \"mem_percent\": %.2f,\n", mem->used_percent);
        } else {
            fputs("    \"mem_percent\": null,\n", f);
        }
        if (disk >= 0.0) {
            fprintf(f, "    \"disk_percent\": %.2f,\n", disk);
        } else {
            fputs("    \"disk_percent\": null,\n", f);
        }
        if (net_ok) {
            fprintf(f, "    \"net_rx_bps\": %.2f,\n", rx);
            fprintf(f, "    \"net_tx_bps\": %.2f\n", tx);
        } else {
            fputs("    \"net_rx_bps\": null,\n", f);
            fputs("    \"net_tx_bps\": null\n", f);
        }
        fputs("  },\n", f);
        fputs("  \"history\": [\n", f);
        for (size_t i = 0; i < hist_n; ++i) {
            fprintf(f,
                    "    {\"index\":%zu,\"cpu_percent\":%.3f,"
                    "\"mem_percent\":%.3f,\"disk_percent\":%.3f,"
                    "\"net_rx_bps\":%.3f,\"net_tx_bps\":%.3f}%s\n",
                    i, cpu_h[i], mem_h[i], disk_h[i], rx_h[i], tx_h[i],
                    (i + 1 < hist_n) ? "," : "");
        }
        fputs("  ],\n", f);
        fprintf(f, "  \"process_count\": %zu\n", proc_count);
        fputs("}\n", f);
        fclose(f);
    }

    char path_utf8[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, stem, -1, path_utf8, sizeof(path_utf8),
                        NULL, NULL);
    snprintf(msg, msg_cap, "exported: %s.{txt,json,csv}", path_utf8);
    return 1;
}

WT_Result wt_tui_run(const WT_CliOptions *opts)
{
    if (!wt_session_is_interactive()) {
        fprintf(stderr,
                "wintune: 'tui' needs an interactive terminal (stdin and stdout).\n"
                "For remote one-shot use: wintune scan --json\n"
                "For a live view over SSH: ssh -t user@host \"wintune tui --safe-terminal\"\n");
        return WT_ERR_NOT_SUPPORTED;
    }

    const int safe = (opts != NULL && opts->safe_terminal) || wt_session_is_remote();
    int color = (opts == NULL || !opts->no_color) && !safe;
    const int unicode = (opts == NULL || !opts->no_unicode) && !safe;
    unsigned int interval = WT_TUI_DEFAULT_INTERVAL_MS;
    if (opts != NULL && opts->interval_ms > 0) {
        interval = (unsigned int)opts->interval_ms;
    }

    WT_TuiTheme theme;
    wt_tui_theme_init(&theme, color, unicode);
    if (opts != NULL && opts->theme != NULL) {
        wt_tui_theme_apply_preset(&theme, opts->theme);
    }

    WT_TuiScreen screen;
    if (wt_tui_screen_init(&screen) != WT_OK) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    WT_ProcessInfo *procs =
        (WT_ProcessInfo *)malloc(WT_TUI_TOP_PROC * sizeof(WT_ProcessInfo));
    WT_ServiceInfo *svcs =
        (WT_ServiceInfo *)malloc(WT_MAX_SERVICES * sizeof(WT_ServiceInfo));
    if (procs == NULL || svcs == NULL) {
        free(procs);
        free(svcs);
        wt_tui_screen_free(&screen);
        return WT_ERR_OUT_OF_MEMORY;
    }
    size_t svc_count = 0;
    int svc_loaded = 0;
    size_t proc_count = 0;
    int procs_ok = 0;

    WT_OsInfo os;
    int os_ok = (wt_collect_os_info(&os) == WT_OK);

    WT_TuiPdh pdh;
    wt_tui_pdh_open(&pdh);

    WT_NetTotals net_prev;
    int net_prev_ok = (wt_collect_net_totals(&net_prev) == WT_OK);
    ULONGLONG t_prev = GetTickCount64();

    WT_TuiState state;
    ZeroMemory(&state, sizeof(state));
    state.view = WT_VIEW_OVERVIEW;
    state.sort = WT_PROCESS_SORT_CPU;
    state.include_network = (opts != NULL && opts->include_network);
    (void)wt_tui_compare_load_pair(&state.compare);
    if (opts != NULL && opts->sort != NULL) {
        if (_wcsicmp(opts->sort, L"memory") == 0 ||
            _wcsicmp(opts->sort, L"mem") == 0) {
            state.sort = WT_PROCESS_SORT_MEMORY;
        } else if (_wcsicmp(opts->sort, L"disk") == 0) {
            state.sort = WT_PROCESS_SORT_DISK;
        } else if (_wcsicmp(opts->sort, L"network") == 0 ||
                   _wcsicmp(opts->sort, L"net") == 0) {
            state.sort = WT_PROCESS_SORT_NETWORK;
            state.include_network = 1;
        } else {
            state.sort = WT_PROCESS_SORT_CPU;
        }
    }

    double frozen_cpu = -1.0, frozen_disk = -1.0;
    WT_MemoryMetrics frozen_mem;
    ZeroMemory(&frozen_mem, sizeof(frozen_mem));
    int frozen_mem_ok = 0;
    WT_NetTotals frozen_net;
    ZeroMemory(&frozen_net, sizeof(frozen_net));
    int frozen_net_ok = 0;
    double frozen_rx = 0.0, frozen_tx = 0.0;

    (void)wt_console_enable_vt();
    SetConsoleCtrlHandler(wt_tui_ctrl_handler, TRUE);
    g_tui_stop = 0;

    fputs("\x1b[?1049h", stdout);
    fputs("\x1b[?25l", stdout);
    fputs("\x1b[2J", stdout);
    fflush(stdout);

    while (!g_tui_stop) {
        int rows = 24, cols = 80;
        if (wt_console_get_size(&rows, &cols) != WT_OK) {
            rows = 24;
            cols = 80;
        }

        int resized = (rows != state.last_rows || cols != state.last_cols);
        state.last_rows = rows;
        state.last_cols = cols;

        int too_small = (cols < WT_TUI_MIN_COLS || rows < WT_TUI_MIN_ROWS);
        int width = cols - 1;
        if (width < WT_TUI_MIN_COLS) {
            width = cols > 2 ? cols - 1 : WT_TUI_MIN_COLS;
        }
        if (width > WT_TUI_MAX_WIDTH) {
            width = WT_TUI_MAX_WIDTH;
        }

        double cpu = frozen_cpu, disk = frozen_disk;
        WT_MemoryMetrics mem = frozen_mem;
        int mem_ok = frozen_mem_ok;
        WT_NetTotals net_now = frozen_net;
        int net_ok = frozen_net_ok;
        double rx = frozen_rx, tx = frozen_tx;

        if (!state.paused) {
            wt_tui_pdh_read(&pdh, &cpu, &disk);
            mem_ok = (wt_collect_memory_metrics(&mem) == WT_OK);

            net_ok = (wt_collect_net_totals(&net_now) == WT_OK);
            ULONGLONG t_now = GetTickCount64();
            double secs = (double)(t_now - t_prev) / 1000.0;
            if (secs <= 0.0) secs = 1.0;
            rx = 0.0;
            tx = 0.0;
            if (net_ok && net_prev_ok) {
                if (net_now.in_bytes >= net_prev.in_bytes) {
                    rx = (double)(net_now.in_bytes - net_prev.in_bytes) / secs;
                }
                if (net_now.out_bytes >= net_prev.out_bytes) {
                    tx = (double)(net_now.out_bytes - net_prev.out_bytes) / secs;
                }
            }
            net_prev = net_now;
            net_prev_ok = net_ok;
            t_prev = t_now;

            wt_tui_hist_push(&state.hist, cpu, mem_ok ? mem.used_percent : 0.0,
                             disk, rx, tx);

            if (state.view == WT_VIEW_OVERVIEW || state.view == WT_VIEW_MEMORY) {
                /* Short sample keeps TUI responsive (was blocking a full interval). */
                if (wt_collect_top_processes(procs, WT_TUI_TOP_PROC, state.sort,
                                             WT_TUI_PROC_SAMPLE_MS,
                                             state.include_network,
                                             &proc_count) == WT_OK) {
                    procs_ok = 1;
                    wt_tui_clamp_scroll(&state.proc_scroll, proc_count, 1);
                } else {
                    proc_count = 0;
                    procs_ok = 0;
                }
            }

            frozen_cpu = cpu;
            frozen_disk = disk;
            frozen_mem = mem;
            frozen_mem_ok = mem_ok;
            frozen_net = net_now;
            frozen_net_ok = net_ok;
            frozen_rx = rx;
            frozen_tx = tx;
        }

        if (state.view == WT_VIEW_SERVICES && !svc_loaded) {
            if (wt_collect_services(svcs, WT_MAX_SERVICES, &svc_count) == WT_OK) {
                svc_loaded = 1;
                wt_tui_clamp_scroll(&state.svc_scroll, svc_count, 1);
            }
        }

        int header_lines = 2;
        int gauge_lines = 4;
        if (state.hist.count > 1 && theme.preset != WT_TUI_THEME_COMPACT) {
            gauge_lines += 5; /* cpu~/ram~/dsk~ + dn~/up~ */
        }
        int title_footer = 4;
        int max_rows = rows - header_lines - gauge_lines - title_footer;
        if (max_rows < 2) max_rows = 2;
        if (theme.preset == WT_TUI_THEME_COMPACT && max_rows > 8) {
            max_rows = 8;
        }

        wt_tui_screen_reset(&screen);

        if (too_small) {
            wt_tui_screen_line(&screen, "%sWinTune TUI%s", wt_tui_bold(&theme),
                               wt_tui_reset(&theme));
            wt_tui_empty_line(&screen, &theme,
                              "Terminal too small — enlarge to at least 48x14.");
            wt_tui_screen_line(&screen, "Current size: %dx%d", cols, rows);
            wt_tui_empty_line(&screen, &theme, "Press q to quit.");
            wt_tui_screen_flush(&screen, stdout);
        } else {
            wt_tui_render_header(&screen, &theme, width, &os, os_ok, &state);
            wt_tui_render_gauges(&screen, &theme, cpu,
                                 mem_ok ? mem.used_percent : 0.0, disk, &mem,
                                 mem_ok, rx, tx, net_ok, &state.hist);
            wt_tui_rule_line(&screen, &theme, width);
            wt_tui_title_line(&screen, &theme, wt_tui_view_title(state.view),
                              width);

            switch (state.view) {
            case WT_VIEW_DISK:
                wt_tui_render_disk(&screen, &theme, disk);
                break;
            case WT_VIEW_MEMORY:
                wt_tui_render_memory(&screen, &theme, &mem, mem_ok, procs,
                                     procs_ok ? proc_count : 0,
                                     state.proc_scroll, max_rows, state.sort);
                break;
            case WT_VIEW_NETWORK:
                wt_tui_render_network(&screen, &theme, &net_now, net_ok, rx, tx);
                break;
            case WT_VIEW_GPU:
                wt_tui_render_gpu(&screen, &theme);
                break;
            case WT_VIEW_POWER:
                wt_tui_render_power(&screen, &theme);
                break;
            case WT_VIEW_SERVICES:
                wt_tui_render_services(&screen, &theme, svcs, svc_count,
                                       svc_loaded, state.svc_scroll, max_rows);
                break;
            case WT_VIEW_COMPARE:
                wt_tui_render_compare(&screen, &theme, &state.compare);
                break;
            case WT_VIEW_HELP:
                wt_tui_render_help(&screen, &theme);
                break;
            default:
                if (!procs_ok && proc_count == 0) {
                    wt_tui_empty_line(&screen, &theme,
                                      "(process list unavailable)");
                } else {
                    wt_tui_render_processes(&screen, &theme, procs, proc_count,
                                            state.proc_scroll, max_rows,
                                            state.sort);
                }
                break;
            }

            wt_tui_rule_line(&screen, &theme, width);
            if (state.status[0] != '\0' &&
                GetTickCount64() < state.status_until_ms) {
                wt_tui_screen_line(&screen, "%s%s%s", wt_tui_cyan(&theme),
                                   state.status, wt_tui_reset(&theme));
            } else {
                wt_tui_screen_line(
                    &screen,
                    "%sKeys:%s q quit | Space pause | t sort | w net | j/k scroll | "
                    "e export | b/a mark | c compare | o/d/m/n/g/p/s views | ? help",
                    wt_tui_dim(&theme), wt_tui_reset(&theme));
            }
            wt_tui_screen_flush(&screen, stdout);
        }

        unsigned int waited = 0;
        int redraw = resized ? 1 : 0;
        while (waited < interval && !g_tui_stop && !redraw) {
            WT_TuiKey key = wt_tui_poll_key();
            switch (key) {
            case WT_TUI_KEY_QUIT:
                g_tui_stop = 1;
                break;
            case WT_TUI_KEY_REFRESH:
                if (state.paused) {
                    state.paused = 0;
                    wt_tui_set_status(&state, "resumed", 1500);
                }
                redraw = 1;
                break;
            case WT_TUI_KEY_PAUSE:
                state.paused = !state.paused;
                wt_tui_set_status(&state,
                                  state.paused ? "paused (Space to resume)"
                                               : "resumed",
                                  2000);
                redraw = 1;
                break;
            case WT_TUI_KEY_HELP:
                state.view = (state.view == WT_VIEW_HELP) ? WT_VIEW_OVERVIEW
                                                          : WT_VIEW_HELP;
                redraw = 1;
                break;
            case WT_TUI_KEY_OVERVIEW:
                state.view = WT_VIEW_OVERVIEW;
                redraw = 1;
                break;
            case WT_TUI_KEY_DISK:
                state.view = WT_VIEW_DISK;
                redraw = 1;
                break;
            case WT_TUI_KEY_MEMORY:
                state.view = WT_VIEW_MEMORY;
                redraw = 1;
                break;
            case WT_TUI_KEY_NETWORK:
                state.view = WT_VIEW_NETWORK;
                redraw = 1;
                break;
            case WT_TUI_KEY_GPU:
                state.view = WT_VIEW_GPU;
                redraw = 1;
                break;
            case WT_TUI_KEY_POWER:
                state.view = WT_VIEW_POWER;
                redraw = 1;
                break;
            case WT_TUI_KEY_SERVICES:
                state.view = WT_VIEW_SERVICES;
                svc_loaded = 0;
                state.svc_scroll = 0;
                redraw = 1;
                break;
            case WT_TUI_KEY_SORT_CYCLE:
                if (state.sort == WT_PROCESS_SORT_CPU) {
                    state.sort = WT_PROCESS_SORT_MEMORY;
                } else if (state.sort == WT_PROCESS_SORT_MEMORY) {
                    state.sort = WT_PROCESS_SORT_DISK;
                } else if (state.sort == WT_PROCESS_SORT_DISK) {
                    state.sort = WT_PROCESS_SORT_NETWORK;
                    state.include_network = 1;
                } else {
                    state.sort = WT_PROCESS_SORT_CPU;
                }
                state.proc_scroll = 0;
                state.paused = 0;
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "sort: %s",
                             wt_tui_sort_name(state.sort));
                    wt_tui_set_status(&state, buf, 1500);
                }
                redraw = 1;
                break;
            case WT_TUI_KEY_SORT_CPU:
                state.sort = WT_PROCESS_SORT_CPU;
                state.proc_scroll = 0;
                state.paused = 0;
                wt_tui_set_status(&state, "sort: cpu", 1500);
                redraw = 1;
                break;
            case WT_TUI_KEY_SORT_MEMORY:
                state.sort = WT_PROCESS_SORT_MEMORY;
                state.proc_scroll = 0;
                state.paused = 0;
                wt_tui_set_status(&state, "sort: memory", 1500);
                redraw = 1;
                break;
            case WT_TUI_KEY_SORT_DISK:
                state.sort = WT_PROCESS_SORT_DISK;
                state.proc_scroll = 0;
                state.paused = 0;
                wt_tui_set_status(&state, "sort: disk", 1500);
                redraw = 1;
                break;
            case WT_TUI_KEY_SORT_NETWORK:
                state.sort = WT_PROCESS_SORT_NETWORK;
                state.include_network = 1;
                state.proc_scroll = 0;
                state.paused = 0;
                wt_tui_set_status(&state, "sort: net (TCP EStats on)", 2000);
                redraw = 1;
                break;
            case WT_TUI_KEY_TOGGLE_NETWORK:
                state.include_network = !state.include_network;
                if (!state.include_network &&
                    state.sort == WT_PROCESS_SORT_NETWORK) {
                    state.sort = WT_PROCESS_SORT_CPU;
                }
                state.paused = 0;
                wt_tui_set_status(&state,
                                  state.include_network
                                      ? "per-process net: on (TCP only)"
                                      : "per-process net: off",
                                  2000);
                redraw = 1;
                break;
            case WT_TUI_KEY_SCROLL_UP:
                if (state.view == WT_VIEW_SERVICES) {
                    if (state.svc_scroll > 0) state.svc_scroll--;
                } else if (state.proc_scroll > 0) {
                    state.proc_scroll--;
                }
                redraw = 1;
                break;
            case WT_TUI_KEY_SCROLL_DOWN:
                if (state.view == WT_VIEW_SERVICES) {
                    state.svc_scroll++;
                } else {
                    state.proc_scroll++;
                }
                redraw = 1;
                break;
            case WT_TUI_KEY_PAGE_UP:
                if (state.view == WT_VIEW_SERVICES) {
                    if (state.svc_scroll > (size_t)max_rows) {
                        state.svc_scroll -= (size_t)max_rows;
                    } else {
                        state.svc_scroll = 0;
                    }
                } else if (state.proc_scroll > (size_t)max_rows) {
                    state.proc_scroll -= (size_t)max_rows;
                } else {
                    state.proc_scroll = 0;
                }
                redraw = 1;
                break;
            case WT_TUI_KEY_PAGE_DOWN:
                if (state.view == WT_VIEW_SERVICES) {
                    state.svc_scroll += (size_t)max_rows;
                } else {
                    state.proc_scroll += (size_t)max_rows;
                }
                redraw = 1;
                break;
            case WT_TUI_KEY_EXPORT: {
                char msg[160];
                if (wt_tui_export_snapshot(&state, cpu, disk, &mem, mem_ok,
                                           rx, tx, net_ok, procs, proc_count,
                                           msg, sizeof(msg))) {
                    wt_tui_set_status(&state, msg, 4000);
                } else {
                    wt_tui_set_status(&state, msg, 3000);
                }
                redraw = 1;
                break;
            }
            case WT_TUI_KEY_MARK_BEFORE: {
                wt_tui_compare_capture(
                    &state.compare.before, cpu,
                    mem_ok ? mem.used_percent : -1.0, disk, rx, tx, net_ok,
                    mem_ok ? mem.used_physical_bytes : 0ULL,
                    mem_ok ? mem.total_physical_bytes : 0ULL, mem_ok);
                if (wt_tui_compare_save_slot(L"before",
                                             &state.compare.before) == WT_OK) {
                    wt_tui_set_status(&state,
                                      "marked before (sample window)", 2500);
                } else {
                    wt_tui_set_status(&state,
                                      "before marked in-session (save failed)",
                                      3000);
                }
                redraw = 1;
                break;
            }
            case WT_TUI_KEY_MARK_AFTER: {
                wt_tui_compare_capture(
                    &state.compare.after, cpu,
                    mem_ok ? mem.used_percent : -1.0, disk, rx, tx, net_ok,
                    mem_ok ? mem.used_physical_bytes : 0ULL,
                    mem_ok ? mem.total_physical_bytes : 0ULL, mem_ok);
                if (wt_tui_compare_save_slot(L"after",
                                             &state.compare.after) == WT_OK) {
                    wt_tui_set_status(&state,
                                      "marked after (sample window)", 2500);
                } else {
                    wt_tui_set_status(&state,
                                      "after marked in-session (save failed)",
                                      3000);
                }
                redraw = 1;
                break;
            }
            case WT_TUI_KEY_COMPARE:
                state.view = (state.view == WT_VIEW_COMPARE) ? WT_VIEW_OVERVIEW
                                                             : WT_VIEW_COMPARE;
                redraw = 1;
                break;
            default:
                break;
            }

            /* Detect resize while waiting. */
            int r2 = rows, c2 = cols;
            if (wt_console_get_size(&r2, &c2) == WT_OK) {
                if (r2 != rows || c2 != cols) {
                    redraw = 1;
                }
            }

            if (!redraw && !g_tui_stop) {
                Sleep(WT_TUI_POLL_STEP_MS);
                waited += WT_TUI_POLL_STEP_MS;
            }
        }
    }

    fputs("\x1b[?25h", stdout);
    fputs("\x1b[?1049l", stdout);
    fflush(stdout);

    SetConsoleCtrlHandler(wt_tui_ctrl_handler, FALSE);
    wt_tui_pdh_close(&pdh);
    free(procs);
    free(svcs);
    wt_tui_screen_free(&screen);
    return WT_OK;
}
