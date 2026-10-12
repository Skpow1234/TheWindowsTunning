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
        if (r->confidence_basis[0] != '\0') {
            fprintf(out, "  Confidence basis: %s\n", r->confidence_basis);
        }
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
        if (report->power.rate_ok && report->power.rate_mw < 0) {
            fprintf(out, "  Discharge: %.1f W",
                    (-(double)report->power.rate_mw) / 1000.0);
            if (report->power.on_ac == 0 && report->power.estimated_seconds > 0) {
                fprintf(out, "  | ~%d min remaining",
                        report->power.estimated_seconds / 60);
            }
            fprintf(out, "\n");
        } else if (report->power.rate_ok && report->power.rate_mw > 0) {
            fprintf(out, "  Charge: %.1f W\n",
                    ((double)report->power.rate_mw) / 1000.0);
        }
        if (report->power.mode_ac_ok || report->power.mode_dc_ok) {
            int mode_ok = 0;
            WT_PowerMode mode = wt_power_active_mode(&report->power, &mode_ok);
            if (mode_ok) {
                fprintf(out, "  Power mode: %s\n", wt_power_mode_label(mode));
            }
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
        if (report->cpu.dpc_percent >= 0.0 ||
            report->cpu.interrupt_percent >= 0.0) {
            fprintf(out, "  DPC: ");
            if (report->cpu.dpc_percent >= 0.0) {
                fprintf(out, "%.1f%%", report->cpu.dpc_percent);
            } else {
                fprintf(out, "n/a");
            }
            fprintf(out, "   Interrupts: ");
            if (report->cpu.interrupt_percent >= 0.0) {
                fprintf(out, "%.1f%%", report->cpu.interrupt_percent);
            } else {
                fprintf(out, "n/a");
            }
            fprintf(out, "\n");
        }
        if (report->cpu.kernel_source_ok &&
            report->cpu.kernel_source[0] != '\0') {
            fprintf(out, "  Loudest kernel source: %s (%s)\n",
                    report->cpu.kernel_source,
                    report->cpu.kernel_source_is_dpc ? "DPC" : "interrupt");
        }
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
        if (report->memory.commit_ok) {
            wchar_t ctot[32];
            wchar_t clim[32];
            wchar_t cpeak[32];
            wt_format_bytes(report->memory.commit_total_bytes, ctot, 32);
            wt_format_bytes(report->memory.commit_limit_bytes, clim, 32);
            wt_format_bytes(report->memory.commit_peak_bytes, cpeak, 32);
            fwprintf(out, L"  Commit: %ls / %ls (%.1f%%)  peak %ls\n",
                     ctot, clim, report->memory.commit_percent, cpeak);
        }
        if (report->memory.standby_ok || report->memory.modified_ok ||
            report->memory.compression_ok) {
            wchar_t list_buf[32];
            fputs("  Lists:", out);
            if (report->memory.standby_ok) {
                wt_format_bytes(report->memory.standby_bytes, list_buf, 32);
                fwprintf(out, L" standby %ls", list_buf);
            }
            if (report->memory.modified_ok) {
                wt_format_bytes(report->memory.modified_bytes, list_buf, 32);
                fwprintf(out, L" modified %ls", list_buf);
            }
            if (report->memory.compression_ok) {
                wt_format_bytes(report->memory.compression_bytes, list_buf, 32);
                fwprintf(out, L" compression %ls", list_buf);
            }
            fputc('\n', out);
        }
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
            if (v->read_latency_count > 0) {
                fprintf(out, " | read p50 %.1f ms p95 %.1f ms",
                        v->read_latency_ms_p50, v->read_latency_ms_p95);
            }
            if (v->write_latency_count > 0) {
                fprintf(out, " | write p50 %.1f ms p95 %.1f ms",
                        v->write_latency_ms_p50, v->write_latency_ms_p95);
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
    if (report->disk_read_latency_count > 0) {
        fprintf(out, "  Total read latency: p50 %.1f ms, p95 %.1f ms (%u samples)\n",
                report->disk_read_latency_ms_p50, report->disk_read_latency_ms_p95,
                report->disk_read_latency_count);
    }
    if (report->disk_write_latency_count > 0) {
        fprintf(out, "  Total write latency: p50 %.1f ms, p95 %.1f ms (%u samples)\n",
                report->disk_write_latency_ms_p50,
                report->disk_write_latency_ms_p95,
                report->disk_write_latency_count);
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
        for (unsigned int mi = 0; mi < report->gpu.display.monitor_count; ++mi) {
            const WT_DisplayMonitor *mon = &report->gpu.display.monitors[mi];
            fwprintf(out, L"    %ls%s",
                     mon->name[0] != L'\0' ? mon->name : L"(monitor)",
                     mon->primary ? L"  primary" : L"");
            if (mon->width > 0 && mon->height > 0) {
                fprintf(out, "  %ux%u", mon->width, mon->height);
            }
            if (mon->refresh_hz >= 0.0) {
                double nearest = (double)(int)(mon->refresh_hz + 0.5);
                if (mon->refresh_hz - nearest < 0.05 &&
                    nearest - mon->refresh_hz < 0.05) {
                    fprintf(out, "  %.0f Hz", mon->refresh_hz);
                } else {
                    fprintf(out, "  %.2f Hz", mon->refresh_hz);
                }
            }
            if (mon->hdr_ok) {
                fprintf(out, "  HDR %s", mon->hdr_enabled ? "on" : "off");
            }
            fprintf(out, "\n");
        }
    }
    fprintf(out, "\n");

    fprintf(out, "Audio:\n");
    if (report->audio_ok && report->audio.available) {
        if (report->audio.endpoint_count == 0) {
            fprintf(out, "  (no active endpoints)\n");
        }
        for (unsigned int ai = 0; ai < report->audio.endpoint_count; ++ai) {
            const WT_AudioEndpoint *ep = &report->audio.endpoints[ai];
            fwprintf(out, L"  %ls  %hs%s",
                     ep->name[0] != L'\0' ? ep->name : L"(endpoint)",
                     ep->render ? "render" : "capture",
                     ep->default_device ? L"  default" : L"");
            if (!ep->probed) {
                fprintf(out, "  (not measured)");
            } else if (ep->exclusive_only) {
                fprintf(out, "  exclusive-only");
            } else if (ep->exclusive) {
                fprintf(out, "  exclusive");
            } else {
                fprintf(out, "  shared");
            }
            fprintf(out, "\n");
        }
        if (report->audio.any_exclusive) {
            fprintf(out,
                    "  Exclusive mode is a latency hint. WinTune does not "
                    "change audio devices.\n");
        }
    } else {
        fprintf(out, "  (endpoints unavailable)\n");
    }
    fprintf(out, "\n");

    fprintf(out, "Search indexer:\n");
    if (!report->search_ok) {
        fprintf(out, "  (unavailable)\n");
    } else {
        if (report->search.service_running == 0) {
            fprintf(out, "  Windows Search service is stopped\n");
        } else if (report->search.service_running < 0) {
            fprintf(out, "  Windows Search service (not reported)\n");
        }
        if (report->search.status_ok) {
            fprintf(out, "  Status: %s",
                    wt_search_status_token(report->search.status));
            if (report->search.paused_reason >= 0) {
                fprintf(out, " (%s)",
                        wt_search_pause_token(report->search.paused_reason));
            }
            fprintf(out, "\n");
        }
        if (report->search.items_ok) {
            fprintf(out, "  Indexed: %ld\n", report->search.items_indexed);
        }
        if (report->search.backlog_ok) {
            fprintf(out,
                    "  Backlog: %ld  (incremental %ld, notifications %ld, "
                    "high priority %ld)\n",
                    report->search.backlog_items,
                    report->search.incremental_count,
                    report->search.notification_count,
                    report->search.high_priority_count);
        }
        if (!report->search.available) {
            fprintf(out, "  (catalog unavailable)\n");
        }
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
            fprintf(out,
                    "  Slow components: %zu (waterfall; see 'wintune boot analyze')\n",
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
            fprintf(out, "  %-6s  %-6s  %-5s  %-12s  %-16s  %-11s  %-8s  %8s  %-10s  %7s  %5s\n",
                    "PID", "PPID", "Sess", "Integrity", "Process", "Origin",
                    "Loc", "CPU%", "Memory", "Handles", "GDI");
        } else {
            fprintf(out, "  %-6s  %-6s  %-5s  %-12s  %-20s  %-11s  %-8s  %-10s  %7s  %5s\n",
                    "PID", "PPID", "Sess", "Integrity", "Process", "Origin",
                    "Loc", "Memory", "Handles", "GDI");
        }
        for (size_t i = 0; i < report->top_process_count; ++i) {
            const WT_ProcessInfo *p = &report->top_processes[i];
            wchar_t mem[32];
            wt_format_bytes(p->working_set_bytes, mem, 32);
            const char *origin = wt_publisher_origin_name(p->identity.origin);
            const char *loc = wt_install_location_name(p->identity.location);
            const char *integrity = wt_integrity_level_name(p->integrity);
            char session[8];
            if (p->session_ok) {
                snprintf(session, sizeof(session), "%lu", p->session_id);
            } else {
                snprintf(session, sizeof(session), "-");
            }
            char loc_mark[16];
            if (p->identity.unusual_location) {
                snprintf(loc_mark, sizeof(loc_mark), "%s!", loc);
            } else {
                snprintf(loc_mark, sizeof(loc_mark), "%s", loc);
            }
            char handles[16];
            char gdi[16];
            if (p->handle_ok) {
                snprintf(handles, sizeof(handles), "%lu", p->handle_count);
            } else {
                snprintf(handles, sizeof(handles), "-");
            }
            if (p->gdi_ok) {
                snprintf(gdi, sizeof(gdi), "%lu", p->gdi_count);
            } else {
                snprintf(gdi, sizeof(gdi), "-");
            }
            if (show_cpu && p->cpu_percent >= 0.0) {
                fprintf(out,
                        "  %6lu  %6lu  %-5s  %-12s  %-16.16ls  %-11s  %-8s  "
                        "%7.1f%%  %-10ls  %7s  %5s\n",
                        p->pid, p->parent_pid, session, integrity, p->name,
                        origin, loc_mark, p->cpu_percent, mem, handles, gdi);
            } else {
                fprintf(out,
                        "  %6lu  %6lu  %-5s  %-12s  %-20.20ls  %-11s  %-8s  "
                        "%-10ls  %7s  %5s\n",
                        p->pid, p->parent_pid, session, integrity, p->name,
                        origin, loc_mark, mem, handles, gdi);
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
        int any_hung = 0;
        for (size_t i = 0; i < report->top_process_count; ++i) {
            if (report->top_processes[i].hung) {
                any_hung = 1;
                break;
            }
        }
        if (any_hung) {
            fprintf(out, "  Wait chain (read-only; WinTune does not end processes):\n");
            for (size_t i = 0; i < report->top_process_count; ++i) {
                const WT_ProcessInfo *p = &report->top_processes[i];
                if (!p->hung) {
                    continue;
                }
                fprintf(out, "    %lu %ls is %s.\n", p->pid, p->name,
                        p->wait_cycle
                            ? "in a deadlock"
                            : "not responding (blocked SendMessage)");
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
