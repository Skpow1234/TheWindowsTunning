#include "tui/tui.h"
#include "tui/tui_screen.h"
#include "tui/tui_theme.h"
#include "tui/tui_widgets.h"
#include "tui/tui_input.h"

#include "platform/console.h"
#include "common/units.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "metrics/network.h"
#include "system/os_info.h"
#include "system/power.h"
#include "system/services.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WT_TUI_DEFAULT_INTERVAL_MS 1000
#define WT_TUI_POLL_STEP_MS 40
#define WT_TUI_PROC_CAP 2048
#define WT_TUI_GAUGE_WIDTH 30

typedef enum WT_TuiView {
    WT_VIEW_OVERVIEW = 0,
    WT_VIEW_DISK,
    WT_VIEW_MEMORY,
    WT_VIEW_NETWORK,
    WT_VIEW_POWER,
    WT_VIEW_SERVICES,
    WT_VIEW_HELP
} WT_TuiView;

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

/* --------------------------------------------------------------------- */
/* Persistent PDH sampler (CPU + disk active time)                        */
/* --------------------------------------------------------------------- */

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
    PdhCollectQueryData(p->query); /* prime baseline */
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

/* --------------------------------------------------------------------- */
/* Rendering helpers                                                      */
/* --------------------------------------------------------------------- */

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

static void wt_tui_render_header(WT_TuiScreen *s, const WT_TuiTheme *t, int width,
                                 const WT_OsInfo *os, int os_ok)
{
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

    wt_tui_screen_line(s, "%s%sWinTune Live%s   Host: %s   Up: %s   Power: %s",
                       wt_tui_bold(t), wt_tui_cyan(t), wt_tui_reset(t),
                       host, uptime, power_str);
    wt_tui_rule_line(s, t, width);
}

static void wt_tui_render_gauges(WT_TuiScreen *s, const WT_TuiTheme *t,
                                 double cpu, double mem_pct, double disk,
                                 const WT_MemoryMetrics *mem, int mem_ok,
                                 double rx, double tx, int net_ok)
{
    char mem_suffix[64] = "";
    if (mem_ok) {
        char used[32], total[32];
        wt_tui_human(mem->used_physical_bytes, used, sizeof(used));
        wt_tui_human(mem->total_physical_bytes, total, sizeof(total));
        snprintf(mem_suffix, sizeof(mem_suffix), "%s / %s", used, total);
    }

    wt_tui_gauge_line(s, t, "CPU", cpu < 0 ? 0.0 : cpu, WT_TUI_GAUGE_WIDTH,
                      cpu < 0 ? "(sampling)" : "");
    wt_tui_gauge_line(s, t, "RAM", mem_ok ? mem_pct : 0.0, WT_TUI_GAUGE_WIDTH,
                      mem_suffix);
    wt_tui_gauge_line(s, t, "DISK", disk < 0 ? 0.0 : disk, WT_TUI_GAUGE_WIDTH,
                      disk < 0 ? "(sampling)" : "active");

    char rxs[24] = "n/a", txs[24] = "n/a";
    if (net_ok) {
        wt_tui_rate(rx, rxs, sizeof(rxs));
        wt_tui_rate(tx, txs, sizeof(txs));
    }
    wt_tui_screen_line(s, "NET   down %-12s  up %-12s", rxs, txs);
}

static void wt_tui_render_processes(WT_TuiScreen *s, const WT_TuiTheme *t,
                                    const WT_ProcessInfo *procs, size_t count,
                                    int max_rows)
{
    wt_tui_screen_line(s, "%s%-6s %-26s %12s %12s%s",
                       wt_tui_dim(t), "PID", "Process", "Memory", "Private",
                       wt_tui_reset(t));
    int shown = 0;
    for (size_t i = 0; i < count && shown < max_rows; ++i, ++shown) {
        char name[64];
        WideCharToMultiByte(CP_UTF8, 0, procs[i].name, -1, name, sizeof(name),
                            NULL, NULL);
        char ws[32], pv[32];
        wt_tui_human(procs[i].working_set_bytes, ws, sizeof(ws));
        wt_tui_human(procs[i].private_bytes, pv, sizeof(pv));
        wt_tui_screen_line(s, "%-6lu %-26.26s %12s %12s",
                           procs[i].pid, name, ws, pv);
    }
}

static void wt_tui_render_disk(WT_TuiScreen *s, const WT_TuiTheme *t, double active)
{
    WT_DiskVolumeMetrics vols[32];
    size_t n = 0;
    wt_tui_screen_line(s, "%s%-8s %16s %16s %10s%s",
                       wt_tui_dim(t), "Volume", "Free", "Total", "Free %",
                       wt_tui_reset(t));
    if (wt_collect_disk_volumes(vols, 32, &n) == WT_OK) {
        for (size_t i = 0; i < n; ++i) {
            char root[16], freeb[32], totalb[32];
            WideCharToMultiByte(CP_UTF8, 0, vols[i].root_path, -1, root,
                                sizeof(root), NULL, NULL);
            wt_tui_human(vols[i].free_bytes, freeb, sizeof(freeb));
            wt_tui_human(vols[i].total_bytes, totalb, sizeof(totalb));
            wt_tui_screen_line(s, "%-8s %16s %16s %9.1f%%",
                               root, freeb, totalb, vols[i].free_percent);
        }
    }
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "Disk active time: %s%.0f%%%s",
                       wt_tui_color_for_pct(t, active < 0 ? 0 : active),
                       active < 0 ? 0.0 : active, wt_tui_reset(t));
}

static void wt_tui_render_memory(WT_TuiScreen *s, const WT_TuiTheme *t,
                                 const WT_MemoryMetrics *mem, int mem_ok,
                                 const WT_ProcessInfo *procs, size_t count,
                                 int max_rows)
{
    if (mem_ok) {
        char total[32], used[32], avail[32];
        wt_tui_human(mem->total_physical_bytes, total, sizeof(total));
        wt_tui_human(mem->used_physical_bytes, used, sizeof(used));
        wt_tui_human(mem->available_physical_bytes, avail, sizeof(avail));
        wt_tui_screen_line(s, "Total: %-12s  Used: %-12s  Available: %-12s  (%.1f%% used)",
                           total, used, avail, mem->used_percent);
    } else {
        wt_tui_screen_line(s, "(memory metrics unavailable)");
    }
    wt_tui_screen_line(s, "");
    wt_tui_render_processes(s, t, procs, count, max_rows);
}

static void wt_tui_render_network(WT_TuiScreen *s, const WT_TuiTheme *t,
                                  const WT_NetTotals *now, int net_ok,
                                  double rx, double tx)
{
    (void)t;
    if (!net_ok) {
        wt_tui_screen_line(s, "(network metrics unavailable)");
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
}

static void wt_tui_render_power(WT_TuiScreen *s, const WT_TuiTheme *t)
{
    WT_PowerInfo p;
    if (wt_collect_power_info(&p) != WT_OK) {
        wt_tui_screen_line(s, "(power information unavailable)");
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
        wt_tui_screen_line(s, "Battery     : %d%%", p.battery_percent);
    }
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "%sUse 'wintune power --set <plan>' to change the plan (Phase 7).%s",
                       wt_tui_dim(t), wt_tui_reset(t));
}

static void wt_tui_render_services(WT_TuiScreen *s, const WT_TuiTheme *t,
                                   const WT_ServiceInfo *svcs, size_t count,
                                   int loaded, int max_rows)
{
    if (!loaded) {
        wt_tui_screen_line(s, "(collecting services...)");
        return;
    }
    size_t running = 0, stopped = 0, autostart = 0;
    for (size_t i = 0; i < count; ++i) {
        if (svcs[i].state == WT_SVC_STATE_RUNNING) running++;
        if (svcs[i].state == WT_SVC_STATE_STOPPED) stopped++;
        if (svcs[i].start_type == WT_SVC_START_AUTO) autostart++;
    }
    wt_tui_screen_line(s, "Total: %zu   running: %zu   stopped: %zu   auto-start: %zu",
                       count, running, stopped, autostart);
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "%s%-34s %-9s %-9s %-7s%s",
                       wt_tui_dim(t), "Name", "State", "Start", "PID",
                       wt_tui_reset(t));
    int shown = 0;
    for (size_t i = 0; i < count && shown < max_rows; ++i) {
        if (svcs[i].state != WT_SVC_STATE_RUNNING) {
            continue; /* show running services in this compact view */
        }
        char name[48];
        WideCharToMultiByte(CP_UTF8, 0, svcs[i].name, -1, name, sizeof(name),
                            NULL, NULL);
        if (svcs[i].pid == 0) {
            wt_tui_screen_line(s, "%-34.34s %-9s %-9s %-7s", name,
                               wt_service_state_name(svcs[i].state),
                               wt_service_start_type_name(svcs[i].start_type), "-");
        } else {
            wt_tui_screen_line(s, "%-34.34s %-9s %-9s %-7lu", name,
                               wt_service_state_name(svcs[i].state),
                               wt_service_start_type_name(svcs[i].start_type),
                               svcs[i].pid);
        }
        shown++;
    }
}

static void wt_tui_render_help(WT_TuiScreen *s, const WT_TuiTheme *t)
{
    wt_tui_screen_line(s, "%sKeyboard%s", wt_tui_bold(t), wt_tui_reset(t));
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "  o   Overview (CPU/RAM/disk/net + top processes)");
    wt_tui_screen_line(s, "  d   Disk volumes and active time");
    wt_tui_screen_line(s, "  m   Memory breakdown");
    wt_tui_screen_line(s, "  n   Network throughput");
    wt_tui_screen_line(s, "  p   Power plan and source");
    wt_tui_screen_line(s, "  s   Services summary");
    wt_tui_screen_line(s, "  r   Refresh now");
    wt_tui_screen_line(s, "  ?/h Toggle this help");
    wt_tui_screen_line(s, "  q   Quit (Esc / Ctrl+C also quit)");
    wt_tui_screen_line(s, "");
    wt_tui_screen_line(s, "%sWinTune never changes the system from the dashboard.%s",
                       wt_tui_dim(t), wt_tui_reset(t));
}

static const char *wt_tui_view_title(WT_TuiView view)
{
    switch (view) {
    case WT_VIEW_DISK:     return "Disk";
    case WT_VIEW_MEMORY:   return "Memory";
    case WT_VIEW_NETWORK:  return "Network";
    case WT_VIEW_POWER:    return "Power";
    case WT_VIEW_SERVICES: return "Services";
    case WT_VIEW_HELP:     return "Help";
    default:               return "Top Processes";
    }
}

/* --------------------------------------------------------------------- */
/* Main loop                                                              */
/* --------------------------------------------------------------------- */

WT_Result wt_tui_run(const WT_CliOptions *opts)
{
    if (!wt_console_is_interactive()) {
        fprintf(stderr,
                "wintune: 'tui' needs an interactive terminal. "
                "Use 'wintune scan' or 'wintune top --watch' instead.\n");
        return WT_ERR_NOT_SUPPORTED;
    }

    const int safe = (opts != NULL && opts->safe_terminal);
    const int color = (opts == NULL || !opts->no_color) && !safe;
    const int unicode = (opts == NULL || !opts->no_unicode) && !safe;
    unsigned int interval = WT_TUI_DEFAULT_INTERVAL_MS;
    if (opts != NULL && opts->interval_ms > 0) {
        interval = (unsigned int)opts->interval_ms;
    }

    WT_TuiTheme theme;
    wt_tui_theme_init(&theme, color, unicode);

    WT_TuiScreen screen;
    if (wt_tui_screen_init(&screen) != WT_OK) {
        return WT_ERR_OUT_OF_MEMORY;
    }

    WT_ProcessInfo *procs =
        (WT_ProcessInfo *)malloc(WT_TUI_PROC_CAP * sizeof(WT_ProcessInfo));
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

    WT_OsInfo os;
    int os_ok = (wt_collect_os_info(&os) == WT_OK);

    WT_TuiPdh pdh;
    wt_tui_pdh_open(&pdh);

    WT_NetTotals net_prev;
    int net_prev_ok = (wt_collect_net_totals(&net_prev) == WT_OK);
    ULONGLONG t_prev = GetTickCount64();

    (void)wt_console_enable_vt();
    SetConsoleCtrlHandler(wt_tui_ctrl_handler, TRUE);
    g_tui_stop = 0;

    fputs("\x1b[?1049h", stdout); /* alternate screen buffer */
    fputs("\x1b[?25l", stdout);   /* hide cursor */
    fputs("\x1b[2J", stdout);     /* clear */
    fflush(stdout);

    WT_TuiView view = WT_VIEW_OVERVIEW;

    while (!g_tui_stop) {
        int rows = 24, cols = 80;
        if (wt_console_get_size(&rows, &cols) != WT_OK) {
            rows = 24;
            cols = 80;
        }
        int width = cols - 1;
        if (width < 60) width = 60;
        if (width > 120) width = 120;

        double cpu = -1.0, disk = -1.0;
        wt_tui_pdh_read(&pdh, &cpu, &disk);

        WT_MemoryMetrics mem;
        int mem_ok = (wt_collect_memory_metrics(&mem) == WT_OK);

        WT_NetTotals net_now;
        int net_ok = (wt_collect_net_totals(&net_now) == WT_OK);
        ULONGLONG t_now = GetTickCount64();
        double secs = (double)(t_now - t_prev) / 1000.0;
        if (secs <= 0.0) secs = 1.0;
        double rx = 0.0, tx = 0.0;
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

        /* Processes are needed by the overview and memory views. */
        size_t proc_count = 0;
        if (view == WT_VIEW_OVERVIEW || view == WT_VIEW_MEMORY) {
            if (wt_collect_processes(procs, WT_TUI_PROC_CAP, &proc_count) == WT_OK) {
                wt_sort_processes_by_memory(procs, proc_count);
            } else {
                proc_count = 0;
            }
        }
        if (view == WT_VIEW_SERVICES && !svc_loaded) {
            if (wt_collect_services(svcs, WT_MAX_SERVICES, &svc_count) == WT_OK) {
                svc_loaded = 1;
            }
        }

        /* Body row budget: total rows minus header(2)+gauges(4)+title(1)+footer(2). */
        int max_rows = rows - 11;
        if (max_rows < 3) max_rows = 3;

        wt_tui_screen_reset(&screen);
        wt_tui_render_header(&screen, &theme, width, &os, os_ok);
        wt_tui_render_gauges(&screen, &theme, cpu, mem_ok ? mem.used_percent : 0.0,
                             disk, &mem, mem_ok, rx, tx, net_ok);
        wt_tui_rule_line(&screen, &theme, width);
        wt_tui_title_line(&screen, &theme, wt_tui_view_title(view), width);

        switch (view) {
        case WT_VIEW_DISK:
            wt_tui_render_disk(&screen, &theme, disk);
            break;
        case WT_VIEW_MEMORY:
            wt_tui_render_memory(&screen, &theme, &mem, mem_ok, procs, proc_count, max_rows);
            break;
        case WT_VIEW_NETWORK:
            wt_tui_render_network(&screen, &theme, &net_now, net_ok, rx, tx);
            break;
        case WT_VIEW_POWER:
            wt_tui_render_power(&screen, &theme);
            break;
        case WT_VIEW_SERVICES:
            wt_tui_render_services(&screen, &theme, svcs, svc_count, svc_loaded, max_rows);
            break;
        case WT_VIEW_HELP:
            wt_tui_render_help(&screen, &theme);
            break;
        default:
            wt_tui_render_processes(&screen, &theme, procs, proc_count, max_rows);
            break;
        }

        wt_tui_rule_line(&screen, &theme, width);
        wt_tui_screen_line(&screen,
                           "%sKeys:%s q quit | r refresh | o overview | d disk | "
                           "m mem | n net | p power | s services | ? help",
                           wt_tui_dim(&theme), wt_tui_reset(&theme));

        wt_tui_screen_flush(&screen, stdout);

        /* Wait for the interval, polling input so the UI stays responsive. */
        unsigned int waited = 0;
        int redraw = 0;
        while (waited < interval && !g_tui_stop && !redraw) {
            WT_TuiKey key = wt_tui_poll_key();
            switch (key) {
            case WT_TUI_KEY_QUIT:     g_tui_stop = 1; break;
            case WT_TUI_KEY_REFRESH:  redraw = 1; break;
            case WT_TUI_KEY_HELP:
                view = (view == WT_VIEW_HELP) ? WT_VIEW_OVERVIEW : WT_VIEW_HELP;
                redraw = 1;
                break;
            case WT_TUI_KEY_OVERVIEW: view = WT_VIEW_OVERVIEW; redraw = 1; break;
            case WT_TUI_KEY_DISK:     view = WT_VIEW_DISK; redraw = 1; break;
            case WT_TUI_KEY_MEMORY:   view = WT_VIEW_MEMORY; redraw = 1; break;
            case WT_TUI_KEY_NETWORK:  view = WT_VIEW_NETWORK; redraw = 1; break;
            case WT_TUI_KEY_POWER:    view = WT_VIEW_POWER; redraw = 1; break;
            case WT_TUI_KEY_SERVICES:
                view = WT_VIEW_SERVICES;
                svc_loaded = 0; /* refresh on entry */
                redraw = 1;
                break;
            default: break;
            }
            if (!redraw && !g_tui_stop) {
                Sleep(WT_TUI_POLL_STEP_MS);
                waited += WT_TUI_POLL_STEP_MS;
            }
        }
    }

    /* Restore the terminal no matter how we exited. */
    fputs("\x1b[?25h", stdout);   /* show cursor */
    fputs("\x1b[?1049l", stdout); /* leave alternate screen */
    fflush(stdout);

    SetConsoleCtrlHandler(wt_tui_ctrl_handler, FALSE);
    wt_tui_pdh_close(&pdh);
    free(procs);
    free(svcs);
    wt_tui_screen_free(&screen);
    return WT_OK;
}
