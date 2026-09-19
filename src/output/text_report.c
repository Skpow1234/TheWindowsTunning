#include "output/text_report.h"
#include "output/table.h"
#include "common/units.h"
#include "platform/time.h"
#include "system/power.h"
#include "system/file_identity.h"

#include <stdio.h>

static void wt_report_section(FILE *out, const char *title)
{
    fputc('\n', out);
    fprintf(out, "%s\n", title);
    for (const char *p = title; *p != '\0'; ++p) {
        fputc('=', out);
    }
    fputc('\n', out);
}

static void wt_report_bottlenecks(FILE *out, const WT_ScanReport *report,
                                  const WT_RecommendationList *recs)
{
    wt_report_section(out, "Top bottlenecks");

    int any = 0;
    if (report->cpu_ok && report->cpu.available &&
        report->cpu.total_usage_percent >= 85.0) {
        fprintf(out, "  - CPU usage was %.1f%% during the sample.\n",
                report->cpu.total_usage_percent);
        any = 1;
    }
    if (report->memory_ok && report->memory.used_percent >= 80.0) {
        fprintf(out, "  - Memory use is %.1f%%.\n", report->memory.used_percent);
        any = 1;
    }
    if (report->disk_active_ok && report->disk_active_percent >= 90.0) {
        fprintf(out, "  - Disk active time was %.0f%% (system total).\n",
                report->disk_active_percent);
        any = 1;
    }
    if (report->gpu_ok && report->gpu.utilization_ok &&
        report->gpu.max_utilization_percent >= 90.0) {
        fprintf(out, "  - GPU engine busy was about %.0f%%.\n",
                report->gpu.max_utilization_percent);
        any = 1;
    }
    if (report->disk_throughput_ok) {
        double thru = 0.0;
        if (report->disk_read_bytes_per_sec > 0.0) {
            thru += report->disk_read_bytes_per_sec;
        }
        if (report->disk_write_bytes_per_sec > 0.0) {
            thru += report->disk_write_bytes_per_sec;
        }
        if (thru >= 50.0 * 1024.0 * 1024.0) {
            fprintf(out, "  - Disk throughput was about %.1f MB/s.\n",
                    thru / (1024.0 * 1024.0));
            any = 1;
        }
    }
    if (report->disk_ok) {
        for (size_t i = 0; i < report->volume_count; ++i) {
            const WT_DiskVolumeMetrics *v = &report->volumes[i];
            if (v->free_percent < 10.0) {
                fwprintf(out,
                         L"  - Drive %ls is low on free space (%.1f%% free).\n",
                         v->root_path, v->free_percent);
                any = 1;
            }
            if (v->activity_ok && v->active_percent >= 90.0) {
                fwprintf(out,
                         L"  - Drive %ls active time was %.0f%%.\n",
                         v->root_path, v->active_percent);
                any = 1;
            }
        }
    }

    if (recs != NULL) {
        for (size_t i = 0; i < recs->count; ++i) {
            const WT_Recommendation *r = &recs->items[i];
            if (r->severity >= WT_SEVERITY_MEDIUM) {
                fprintf(out, "  - [%s] %s\n", r->id, r->title);
                any = 1;
            }
        }
    }

    if (!any) {
        fprintf(out, "  No significant bottlenecks detected from the current samples.\n");
    }
}

static void wt_report_risk_notes(FILE *out, const WT_RecommendationList *recs)
{
    wt_report_section(out, "Risk notes");

    if (recs == NULL || recs->count == 0) {
        fprintf(out, "  No recommendations; no action risks to note.\n");
        return;
    }

    int any = 0;
    for (size_t i = 0; i < recs->count; ++i) {
        const WT_Recommendation *r = &recs->items[i];
        if (r->risk >= WT_RISK_LOW || r->requires_admin) {
            fprintf(out, "  [%s] risk=%s", r->id, wt_risk_to_string(r->risk));
            if (r->requires_admin) {
                fputs(" requires_admin", out);
            }
            if (r->rollback_available) {
                fputs(" reversible", out);
            }
            fputc('\n', out);
            any = 1;
        }
    }
    if (!any) {
        fprintf(out, "  Current recommendations are informational or low risk.\n");
    }
}

void wt_print_performance_report_text(FILE *out,
                                      const WT_ScanReport *report,
                                      const WT_RecommendationList *recs)
{
    if (out == NULL || report == NULL) {
        return;
    }

    char ts[32];
    if (wt_now_iso8601_utc(ts, sizeof(ts)) != WT_OK) {
        ts[0] = '\0';
    }

    fprintf(out, "WinTune Performance Report\n");
    if (ts[0] != '\0') {
        fprintf(out, "Generated: %s\n", ts);
    }

    wt_report_section(out, "System summary");
    if (report->os_ok) {
        wchar_t uptime[32];
        wt_format_duration_ms(report->os.uptime_ms, uptime, 32);
        fwprintf(out, L"  OS:      %ls %ls\n", report->os.product_name, report->os.arch);
        fwprintf(out, L"  Host:    %ls\n", report->os.hostname);
        fwprintf(out, L"  Uptime:  %ls\n", uptime);
    } else {
        fputs("  OS information unavailable.\n", out);
    }

    wt_report_section(out, "Performance summary");
    if (report->cpu_ok && report->cpu.available) {
        fprintf(out, "  CPU:     %.1f%% (%u logical processors)\n",
                report->cpu.total_usage_percent,
                report->cpu.logical_processor_count);
    } else {
        fprintf(out, "  CPU:     unavailable\n");
    }
    if (report->memory_ok) {
        wchar_t used[32], total[32];
        wt_format_bytes(report->memory.used_physical_bytes, used, 32);
        wt_format_bytes(report->memory.total_physical_bytes, total, 32);
        fwprintf(out, L"  Memory:  %ls / %ls (%.1f%% used)\n",
                 used, total, report->memory.used_percent);
    } else {
        fputs("  Memory:  unavailable\n", out);
    }
    if (report->disk_ok && report->volume_count > 0) {
        for (size_t i = 0; i < report->volume_count; ++i) {
            const WT_DiskVolumeMetrics *v = &report->volumes[i];
            wchar_t free_b[32], total_b[32];
            wt_format_bytes(v->free_bytes, free_b, 32);
            wt_format_bytes(v->total_bytes, total_b, 32);
            fwprintf(out, L"  Disk %ls %ls free / %ls (%.1f%% free)",
                     v->root_path, free_b, total_b, v->free_percent);
            if (v->activity_ok) {
                fprintf(out, " | active %.0f%%", v->active_percent);
            }
            if (v->throughput_ok) {
                wchar_t rd[32], wr[32];
                wt_format_bytes((unsigned long long)v->read_bytes_per_sec, rd,
                                32);
                wt_format_bytes((unsigned long long)v->write_bytes_per_sec, wr,
                                32);
                fwprintf(out, L" | %ls/s read, %ls/s write", rd, wr);
            }
            if (v->queue_ok) {
                fprintf(out, " | queue %.2f", v->avg_queue_length);
            }
            fprintf(out, "\n");
        }
    }
    if (report->disk_active_ok) {
        fprintf(out, "  Disk total active time: %.0f%%",
                report->disk_active_percent);
        if (report->disk_active_ok_samples > 1u) {
            fprintf(out, " (avg of %u, peak %.0f%%, %u hot)",
                    report->disk_active_ok_samples,
                    report->disk_active_max_percent >= 0.0
                        ? report->disk_active_max_percent
                        : report->disk_active_percent,
                    report->disk_active_hot_samples);
        }
        fprintf(out, "\n");
    }
    if (report->disk_throughput_ok) {
        wchar_t rd[32], wr[32];
        wt_format_bytes((unsigned long long)report->disk_read_bytes_per_sec,
                        rd, 32);
        wt_format_bytes((unsigned long long)report->disk_write_bytes_per_sec,
                        wr, 32);
        fwprintf(out, L"  Disk total throughput: %ls/s read, %ls/s write\n", rd,
                 wr);
    }
    if (report->disk_queue_ok) {
        fprintf(out, "  Disk total avg. queue length: %.2f\n",
                report->disk_avg_queue_length);
    }
    if (report->gpu_ok && report->gpu.adapters_ok) {
        for (size_t i = 0; i < report->gpu.adapter_count; ++i) {
            const WT_GpuAdapter *a = &report->gpu.adapters[i];
            wchar_t ded[32];
            wt_format_bytes(a->dedicated_bytes, ded, 32);
            fwprintf(out, L"  GPU %ls  dedicated %ls", a->name, ded);
            if (a->utilization_ok) {
                fprintf(out, "  | busy %.0f%%", a->utilization_percent);
            }
            fprintf(out, "\n");
        }
    }
    if (report->gpu_ok && report->gpu.display.available) {
        fprintf(out, "  Displays: %u active", report->gpu.display.display_count);
        if (report->gpu.display.primary_width > 0) {
            fprintf(out, "  | primary %ux%u",
                    report->gpu.display.primary_width,
                    report->gpu.display.primary_height);
        }
        fprintf(out, "\n");
    }

    if (report->boot_ok && report->boot.boot_duration_ms > 0) {
        wchar_t boot_dur[32];
        wt_format_duration_ms(report->boot.boot_duration_ms, boot_dur, 32);
        fwprintf(out, L"  Last boot: %ls (%hs)", boot_dur,
                 wt_boot_kind_name(report->boot.last_boot_kind));
        if (report->boot.is_degraded) {
            fputs(" (degradation detected)", out);
        }
        fputc('\n', out);
        if (report->boot.history.count > 1) {
            fprintf(out,
                    "  Boot history: %zu samples, avg %.1f s (%u slow)\n",
                    report->boot.history.count,
                    report->boot.history.avg_duration_ms / 1000.0,
                    report->boot.history.slow_count);
        }
    }

    wt_report_bottlenecks(out, report, recs);

    wt_report_section(out, "Power plan");
    if (report->power_ok) {
        fprintf(out, "  Plan:    %s (%ls)\n",
                wt_power_scheme_name(report->power.scheme),
                report->power.active_name);
        if (report->power.on_ac == 1) {
            fputs("  Source:  AC power", out);
            if (report->power.charging == 1) {
                fputs(" (charging)", out);
            }
            fputc('\n', out);
        } else if (report->power.on_ac == 0) {
            fprintf(out, "  Source:  Battery (%d%%)\n",
                    report->power.battery_percent);
            if (report->power.rate_ok && report->power.discharging == 1 &&
                report->power.rate_mw < 0) {
                fprintf(out, "  Discharge: %.1f W\n",
                        (-(double)report->power.rate_mw) / 1000.0);
            }
        }
        if (report->power.processor_capped) {
            int cap = (report->power.on_ac == 1)
                          ? report->power.processor_max_pct_ac
                          : report->power.processor_max_pct_dc;
            fprintf(out, "  Processor max (plan): %d%%\n",
                    cap >= 0 ? cap : 0);
        }
    } else {
        fputs("  Power information unavailable.\n", out);
    }

    wt_report_section(out, "Top processes (by memory)");
    if (report->processes_ok && report->top_process_count > 0) {
        /* Table helpers write to stdout; emit a compact list to the report file. */
        for (size_t i = 0; i < report->top_process_count; ++i) {
            const WT_ProcessInfo *p = &report->top_processes[i];
            wchar_t mem[32];
            wt_format_bytes(p->working_set_bytes, mem, 32);
            fprintf(out, "  %6lu  %-24.24ls  %-11s  %-8s%s  %ls\n",
                    p->pid, p->name,
                    wt_publisher_origin_name(p->identity.origin),
                    wt_install_location_name(p->identity.location),
                    p->identity.unusual_location ? "!" : "",
                    mem);
            if (p->identity.product_name[0] != L'\0') {
                fprintf(out, "         Product: %.60ls\n",
                        p->identity.product_name);
            }
            if (p->identity.path[0] != L'\0') {
                fprintf(out, "         Path: %.90ls\n", p->identity.path);
            }
        }
    } else {
        fputs("  Process data unavailable.\n", out);
    }

    wt_report_section(out, "Recommendations");
    if (recs == NULL || recs->count == 0) {
        fputs("  None. No performance issues detected from the current samples.\n", out);
    } else {
        for (size_t i = 0; i < recs->count; ++i) {
            const WT_Recommendation *r = &recs->items[i];
            fprintf(out, "  [%s] %s\n", r->id, r->title);
            fprintf(out, "    Severity: %s | Risk: %s | Confidence: %d%%\n",
                    wt_severity_to_string(r->severity),
                    wt_risk_to_string(r->risk),
                    r->confidence_percent);
            if (r->confidence_basis[0] != '\0') {
                fprintf(out, "    Confidence basis: %s\n", r->confidence_basis);
            }
            fprintf(out, "    Why: %s\n", r->reason);
            fprintf(out, "    Action: %s\n", r->action);
        }
    }

    wt_report_risk_notes(out, recs);

    wt_report_section(out, "Further inspection");
    fputs("  Boot analysis:   wintune boot analyze\n", out);
    fputs("  Startup impact:  wintune startup --measured\n", out);
    fputs("  Service status:  wintune services\n", out);
    fputs("  Live monitor:    wintune top --watch\n", out);
    fputs("  Apply a fix:     wintune apply <id>   (after reviewing recommendations)\n",
          out);
}
