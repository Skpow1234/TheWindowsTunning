#include "output/text.h"
#include "output/table.h"
#include "common/units.h"
#include "system/power.h"
#include "system/file_identity.h"
#include "platform/time.h"

#include <stdio.h>

static void wt_print_recommendations_to(FILE *out, const WT_RecommendationList *recs)
{
    fprintf(out, "Recommendations:\n");
    if (recs == NULL || recs->count == 0) {
        fprintf(out,
                "  None. No performance issues detected from the current samples.\n");
        return;
    }

    for (size_t i = 0; i < recs->count; ++i) {
        const WT_Recommendation *r = &recs->items[i];
        fprintf(out, "[%s] %s\n", r->id, r->title);
        fprintf(out, "  Severity: %s | Risk: %s | Confidence: %d%%%s%s\n",
                wt_severity_to_string(r->severity),
                wt_risk_to_string(r->risk),
                r->confidence_percent,
                r->requires_admin ? " | requires admin" : "",
                r->rollback_available ? " | reversible" : "");
        fprintf(out, "  Why: %s\n", r->reason);
        fprintf(out, "  Action: %s\n", r->action);
        if (i + 1 < recs->count) {
            fprintf(out, "\n");
        }
    }
}

void wt_print_recommendations_text(const WT_RecommendationList *recs)
{
    wt_print_recommendations_to(stdout, recs);
}

void wt_print_doctor_summary_text(FILE *out, const WT_RecommendationList *recs)
{
    if (out == NULL) {
        return;
    }

    size_t high = 0, medium = 0, low = 0;
    size_t count = (recs != NULL) ? recs->count : 0;
    for (size_t i = 0; i < count; ++i) {
        switch (recs->items[i].severity) {
        case WT_SEVERITY_CRITICAL:
        case WT_SEVERITY_HIGH:   high++;   break;
        case WT_SEVERITY_MEDIUM: medium++; break;
        default:                 low++;    break;
        }
    }

    fprintf(out, "\nSummary: %zu recommendation(s)", count);
    if (count > 0) {
        fprintf(out, " - %zu high, %zu medium, %zu low/info", high, medium, low);
    }
    fprintf(out, "\n");
    if (high > 0) {
        fprintf(out, "Start with the high-severity items above.\n");
    } else if (count == 0) {
        fprintf(out, "Nothing needs attention based on the current samples.\n");
    }
}

void wt_print_scan_report_text_to(FILE *out, const WT_ScanReport *report,
                                  const WT_RecommendationList *recs)
{
    if (out == NULL || report == NULL) {
        return;
    }

    fprintf(out, "WinTune System Scan\n\n");

    if (report->os_ok) {
        wchar_t uptime[32];
        wt_format_duration_ms(report->os.uptime_ms, uptime, 32);
        fwprintf(out, L"OS: %ls %ls\n", report->os.product_name, report->os.arch);
        fwprintf(out, L"Host: %ls\n", report->os.hostname);
        fwprintf(out, L"Uptime: %ls\n", uptime);
    } else {
        fprintf(out, "OS: (unavailable)\n");
    }

    if (report->power_ok) {
        fprintf(out, "Power: %s", wt_power_scheme_name(report->power.scheme));
        if (report->power.on_ac == 1) {
            fprintf(out, " (AC");
            if (report->power.charging == 1) {
                fprintf(out, ", charging");
            }
        } else if (report->power.on_ac == 0) {
            fprintf(out, " (battery");
        } else {
            fprintf(out, " (");
        }
        if (report->power.battery_percent >= 0) {
            fprintf(out, " %d%%)", report->power.battery_percent);
        } else {
            fprintf(out, ")");
        }
        fprintf(out, "\n");
        if (report->power.rate_ok && report->power.on_ac == 0 &&
            report->power.discharging == 1 && report->power.rate_mw < 0) {
            fprintf(out, "  Discharge: %.1f W",
                    (-(double)report->power.rate_mw) / 1000.0);
            if (report->power.estimated_seconds > 0) {
                fprintf(out, "  | ~%d min remaining",
                        report->power.estimated_seconds / 60);
            }
            fprintf(out, "\n");
        }
        if (report->power.processor_capped) {
            int cap = (report->power.on_ac == 1)
                          ? report->power.processor_max_pct_ac
                          : report->power.processor_max_pct_dc;
            fprintf(out, "  Processor max (plan): %d%%\n",
                    cap >= 0 ? cap : 0);
        }
    }
    fprintf(out, "\n");

    fprintf(out, "CPU:\n");
    if (report->cpu_ok && report->cpu.available) {
        fprintf(out, "  Usage: %.1f%%", report->cpu.total_usage_percent);
        if (report->scan_sample_count > 1) {
            fprintf(out, " (avg of %u samples)", report->scan_sample_count);
        }
        fprintf(out, "\n");
    } else {
        fprintf(out, "  Usage: (unavailable)\n");
    }
    fprintf(out, "  Logical processors: %u\n", report->cpu.logical_processor_count);
    fprintf(out, "\n");

    fprintf(out, "Memory:\n");
    if (report->memory_ok) {
        wchar_t used[32];
        wchar_t total[32];
        wt_format_bytes(report->memory.used_physical_bytes, used, 32);
        wt_format_bytes(report->memory.total_physical_bytes, total, 32);
        fwprintf(out, L"  Used: %ls / %ls (%.1f%%)\n", used, total,
                 report->memory.used_percent);
    } else {
        fprintf(out, "  (unavailable)\n");
    }
    fprintf(out, "\n");

    fprintf(out, "Disk:\n");
    if (report->disk_ok && report->volume_count > 0) {
        for (size_t i = 0; i < report->volume_count; ++i) {
            const WT_DiskVolumeMetrics *v = &report->volumes[i];
            wchar_t free_bytes[32];
            wchar_t total_bytes[32];
            wt_format_bytes(v->free_bytes, free_bytes, 32);
            wt_format_bytes(v->total_bytes, total_bytes, 32);
            fwprintf(out, L"  %ls %ls free / %ls (%.1f%% free)",
                     v->root_path, free_bytes, total_bytes, v->free_percent);
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
    } else {
        fprintf(out, "  (unavailable)\n");
    }
    if (report->disk_active_ok) {
        fprintf(out, "  Total active time: %.0f%%", report->disk_active_percent);
        if (report->disk_active_ok_samples > 1u) {
            fprintf(out, " (avg of %u samples", report->disk_active_ok_samples);
            if (report->disk_active_max_percent >= 0.0) {
                fprintf(out, ", peak %.0f%%", report->disk_active_max_percent);
            }
            fprintf(out, ", %u hot >= %.0f%%)",
                    report->disk_active_hot_samples, 90.0);
        } else if (report->scan_sample_count > 1) {
            fprintf(out, " (avg of %u samples)", report->scan_sample_count);
        }
        fprintf(out, "\n");
    }
    if (report->disk_throughput_ok) {
        wchar_t rd[32], wr[32];
        wt_format_bytes((unsigned long long)report->disk_read_bytes_per_sec,
                        rd, 32);
        wt_format_bytes((unsigned long long)report->disk_write_bytes_per_sec,
                        wr, 32);
        fwprintf(out, L"  Total throughput: %ls/s read, %ls/s write\n", rd, wr);
    }
    if (report->disk_queue_ok) {
        fprintf(out, "  Total avg. queue length: %.2f\n",
                report->disk_avg_queue_length);
    }
    fprintf(out, "\n");

    fprintf(out, "GPU / Display:\n");
    if (report->gpu_ok && report->gpu.adapters_ok &&
        report->gpu.adapter_count > 0) {
        for (size_t i = 0; i < report->gpu.adapter_count; ++i) {
            const WT_GpuAdapter *a = &report->gpu.adapters[i];
            wchar_t ded[32];
            wt_format_bytes(a->dedicated_bytes, ded, 32);
            fwprintf(out, L"  %ls  dedicated %ls", a->name, ded);
            if (a->utilization_ok) {
                fprintf(out, "  | busy %.0f%%", a->utilization_percent);
            }
            fprintf(out, "\n");
        }
    } else {
        fprintf(out, "  (adapters unavailable)\n");
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
    fprintf(out, "\n");

    if (report->boot_ok && report->boot.boot_duration_ms > 0) {
        wchar_t boot_dur[32];
        wt_format_duration_ms(report->boot.boot_duration_ms, boot_dur, 32);
        fwprintf(out, L"Last boot: %ls (%hs)", boot_dur,
                 wt_boot_kind_name(report->boot.last_boot_kind));
        if (report->boot.is_degraded) {
            fprintf(out, " (degradation detected)");
        }
        fprintf(out, "\n");
        if (report->boot.history.count > 1) {
            fprintf(out,
                    "  History: %zu boots, avg %.1f s, %u slow, "
                    "%u cold / %u warm\n",
                    report->boot.history.count,
                    report->boot.history.avg_duration_ms / 1000.0,
                    report->boot.history.slow_count,
                    report->boot.history.cold_count,
                    report->boot.history.warm_count);
        }
        if (report->boot.component_count > 0) {
            fprintf(out, "  Slow components: %zu (see 'wintune boot analyze')\n",
                    report->boot.component_count);
        }
        fprintf(out, "\n");
    }

    if (report->updates_ok && report->updates.reboot_required) {
        fprintf(out, "Updates: reboot pending");
        if (report->updates.reboot_wu) {
            fprintf(out, " (Windows Update)");
        }
        if (report->updates.reboot_cbs) {
            fprintf(out, " (servicing)");
        }
        fprintf(out, "\n  Run 'wintune updates' for details.\n\n");
    }

    fprintf(out, "Top Processes by Memory:\n");
    if (report->processes_ok && report->top_process_count > 0) {
        int show_cpu = 0;
        for (size_t i = 0; i < report->top_process_count; ++i) {
            if (report->top_processes[i].cpu_percent >= 0.0) {
                show_cpu = 1;
                break;
            }
        }
        if (show_cpu) {
            fprintf(out, "  %-6s  %-20s  %-11s  %-8s  %8s  %s\n", "PID",
                    "Process", "Origin", "Loc", "CPU%", "Memory");
        } else {
            fprintf(out, "  %-6s  %-24s  %-11s  %-8s  %s\n", "PID", "Process",
                    "Origin", "Loc", "Memory");
        }
        for (size_t i = 0; i < report->top_process_count; ++i) {
            const WT_ProcessInfo *p = &report->top_processes[i];
            wchar_t mem[32];
            wt_format_bytes(p->working_set_bytes, mem, 32);
            const char *origin = wt_publisher_origin_name(p->identity.origin);
            const char *loc = wt_install_location_name(p->identity.location);
            char loc_mark[16];
            if (p->identity.unusual_location) {
                snprintf(loc_mark, sizeof(loc_mark), "%s!", loc);
            } else {
                snprintf(loc_mark, sizeof(loc_mark), "%s", loc);
            }
            if (show_cpu && p->cpu_percent >= 0.0) {
                fprintf(out, "  %6lu  %-20.20ls  %-11s  %-8s  %7.1f%%  %ls\n",
                        p->pid, p->name, origin, loc_mark, p->cpu_percent, mem);
            } else {
                fprintf(out, "  %6lu  %-24.24ls  %-11s  %-8s  %ls\n",
                        p->pid, p->name, origin, loc_mark, mem);
            }
            if (p->identity.product_name[0] != L'\0' ||
                p->identity.unusual_location) {
                fprintf(out, "         ");
                if (p->identity.product_name[0] != L'\0') {
                    fprintf(out, "%.48ls", p->identity.product_name);
                    if (p->identity.path[0] != L'\0') {
                        fprintf(out, " — ");
                    }
                }
                if (p->identity.path[0] != L'\0') {
                    fprintf(out, "%.60ls", p->identity.path);
                }
                fprintf(out, "\n");
            }
        }
    } else {
        fprintf(out, "  (unavailable)\n");
    }

    if (recs != NULL) {
        fprintf(out, "\n");
        wt_print_recommendations_to(out, recs);
    }
}

void wt_print_scan_report_text(const WT_ScanReport *report,
                               const WT_RecommendationList *recs)
{
    wt_print_scan_report_text_to(stdout, report, recs);
}
