#include "output/text_report.h"
#include "output/table.h"
#include "common/units.h"
#include "platform/time.h"
#include "system/power.h"

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
        fprintf(out, "  - Disk active time was %.0f%%.\n",
                report->disk_active_percent);
        any = 1;
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
            fwprintf(out, L"  Disk %ls %ls free / %ls (%.1f%% free)\n",
                     v->root_path, free_b, total_b, v->free_percent);
        }
    }
    if (report->disk_active_ok) {
        fprintf(out, "  Disk active time: %.0f%%\n", report->disk_active_percent);
    }

    wt_report_bottlenecks(out, report, recs);

    wt_report_section(out, "Power plan");
    if (report->power_ok) {
        fprintf(out, "  Plan:    %s (%ls)\n",
                wt_power_scheme_name(report->power.scheme),
                report->power.active_name);
        if (report->power.on_ac == 1) {
            fputs("  Source:  AC power\n", out);
        } else if (report->power.on_ac == 0) {
            fprintf(out, "  Source:  Battery (%d%%)\n",
                    report->power.battery_percent);
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
            fwprintf(out, L"  %6lu  %-32.32ls  %ls\n", p->pid, p->name, mem);
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
            fprintf(out, "    Why: %s\n", r->reason);
            fprintf(out, "    Action: %s\n", r->action);
        }
    }

    wt_report_risk_notes(out, recs);

    wt_report_section(out, "Further inspection");
    fputs("  Startup impact:  wintune startup\n", out);
    fputs("  Service status:  wintune services\n", out);
    fputs("  Live monitor:    wintune top --watch\n", out);
    fputs("  Apply a fix:     wintune apply <id>   (after reviewing recommendations)\n",
          out);
}
