#include "output/text.h"
#include "output/table.h"
#include "common/units.h"
#include "system/power.h"
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
            fwprintf(out, L"  %ls %ls free / %ls (%.1f%% free)\n",
                     v->root_path, free_bytes, total_bytes, v->free_percent);
        }
    } else {
        fprintf(out, "  (unavailable)\n");
    }
    if (report->disk_active_ok) {
        fprintf(out, "  Active time: %.0f%%", report->disk_active_percent);
        if (report->scan_sample_count > 1) {
            fprintf(out, " (avg of %u samples)", report->scan_sample_count);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "\n");

    if (report->boot_ok && report->boot.boot_duration_ms > 0) {
        wchar_t boot_dur[32];
        wt_format_duration_ms(report->boot.boot_duration_ms, boot_dur, 32);
        fwprintf(out, L"Last boot: %ls", boot_dur);
        if (report->boot.is_degraded) {
            fprintf(out, " (degradation detected)");
        }
        fprintf(out, "\n");
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
            fwprintf(out, L"  %-6s  %-28.28ls  %8s  %ls\n", L"PID", L"Process",
                     L"CPU%", L"Memory");
        }
        for (size_t i = 0; i < report->top_process_count; ++i) {
            const WT_ProcessInfo *p = &report->top_processes[i];
            wchar_t mem[32];
            wt_format_bytes(p->working_set_bytes, mem, 32);
            if (show_cpu && p->cpu_percent >= 0.0) {
                fwprintf(out, L"  %6lu  %-28.28ls  %7.1f%%  %ls\n",
                         p->pid, p->name, p->cpu_percent, mem);
            } else {
                fwprintf(out, L"  %6lu  %-32.32ls  %ls\n", p->pid, p->name, mem);
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
