#include "core/scan.h"

#include "metrics/cpu.h"
#include "metrics/memory.h"
#include "metrics/disk.h"
#include "metrics/process.h"
#include "metrics/gpu.h"
#include "metrics/network.h"
#include "system/audio.h"
#include "system/search.h"
#include "system/defender.h"
#include "system/os_info.h"
#include "system/power.h"
#include "system/boot.h"
#include "system/updates.h"
#include "common/error.h"
#include "common/log.h"

#include <string.h>
#include <windows.h>

#define WT_SCAN_DEFAULT_SAMPLE_MS 500
#define WT_SCAN_DEFAULT_TOP_LIMIT 10
#define WT_SCAN_DEFAULT_SAMPLE_COUNT 1
#define WT_SCAN_DEFAULT_SAMPLE_INTERVAL_MS 1000
#define WT_SCAN_MAX_SAMPLES 32

static void wt_scan_collect_top_processes(WT_ScanReport *report, size_t top_limit,
                                          unsigned int sample_ms)
{
    if (top_limit > WT_MAX_TOP_PROCESSES) {
        top_limit = WT_MAX_TOP_PROCESSES;
    }

    size_t count = 0;
    double user_cpu_max = -1.0;
    double user_cpu_sum = -1.0;
    if (wt_collect_top_processes(report->top_processes, top_limit,
                                 WT_PROCESS_SORT_MEMORY, sample_ms, 0,
                                 &count, &user_cpu_max, &user_cpu_sum) == WT_OK) {
        report->top_process_count = count;
        report->processes_ok = 1;
        if (report->cpu_ok && report->cpu.available) {
            wt_cpu_attribute_kernel_source(&report->cpu, user_cpu_max,
                                           user_cpu_sum);
        }
    }
}

static void wt_scan_merge_sample(WT_ScanReport *acc, const WT_ScanReport *sample,
                                 unsigned int sample_index,
                                 unsigned int sample_total)
{
    unsigned int scan_samples = acc->scan_sample_count;
    unsigned int scan_interval = acc->scan_sample_interval_ms;

    if (sample_index == 0) {
        *acc = *sample;
        acc->scan_sample_count = scan_samples;
        acc->scan_sample_interval_ms = scan_interval;
        /* Seed sustained-disk counters from the first sample (Phase 30). */
        acc->disk_active_ok_samples = 0;
        acc->disk_active_hot_samples = 0;
        acc->disk_active_max_percent = -1.0;
        if (sample->disk_active_ok) {
            acc->disk_active_ok_samples = 1;
            acc->disk_active_max_percent = sample->disk_active_percent;
            if (sample->disk_active_percent >= 90.0) {
                acc->disk_active_hot_samples = 1;
            }
        }
        /* Phase 35: CPU / memory sample stats. */
        acc->cpu_ok_samples = 0;
        acc->cpu_hot_samples = 0;
        acc->cpu_max_percent = -1.0;
        if (sample->cpu_ok && sample->cpu.available) {
            acc->cpu_ok_samples = 1;
            acc->cpu_max_percent = sample->cpu.total_usage_percent;
            if (sample->cpu.total_usage_percent >= 85.0) {
                acc->cpu_hot_samples = 1;
            }
        }
        acc->memory_ok_samples = 0;
        acc->memory_pressure_samples = 0;
        acc->memory_max_used_percent = -1.0;
        if (sample->memory_ok) {
            acc->memory_ok_samples = 1;
            acc->memory_max_used_percent = sample->memory.used_percent;
            if (sample->memory.total_physical_bytes > 0) {
                double avail = (double)sample->memory.available_physical_bytes *
                               100.0 /
                               (double)sample->memory.total_physical_bytes;
                if (avail < 20.0) {
                    acc->memory_pressure_samples = 1;
                }
            }
        }
        return;
    }

    if (sample->cpu_ok && sample->cpu.available) {
        if (acc->cpu_ok) {
            acc->cpu.total_usage_percent =
                ((acc->cpu.total_usage_percent * (double)acc->cpu_ok_samples) +
                 sample->cpu.total_usage_percent) /
                (double)(acc->cpu_ok_samples + 1);
            if (sample->cpu.dpc_percent >= 0.0) {
                if (acc->cpu.dpc_percent >= 0.0) {
                    acc->cpu.dpc_percent =
                        ((acc->cpu.dpc_percent * (double)acc->cpu_ok_samples) +
                         sample->cpu.dpc_percent) /
                        (double)(acc->cpu_ok_samples + 1);
                } else {
                    acc->cpu.dpc_percent = sample->cpu.dpc_percent;
                }
            }
            if (sample->cpu.interrupt_percent >= 0.0) {
                if (acc->cpu.interrupt_percent >= 0.0) {
                    acc->cpu.interrupt_percent =
                        ((acc->cpu.interrupt_percent *
                          (double)acc->cpu_ok_samples) +
                         sample->cpu.interrupt_percent) /
                        (double)(acc->cpu_ok_samples + 1);
                } else {
                    acc->cpu.interrupt_percent = sample->cpu.interrupt_percent;
                }
            }
            if (sample->cpu.kernel_source_ok) {
                acc->cpu.kernel_source_ok = 1;
                acc->cpu.kernel_source_is_dpc = sample->cpu.kernel_source_is_dpc;
                memcpy(acc->cpu.kernel_source, sample->cpu.kernel_source,
                       sizeof(acc->cpu.kernel_source));
            }
        } else {
            acc->cpu = sample->cpu;
            acc->cpu_ok = 1;
            acc->cpu.available = 1;
        }
        acc->cpu_ok_samples++;
        if (acc->cpu_max_percent < 0.0 ||
            sample->cpu.total_usage_percent > acc->cpu_max_percent) {
            acc->cpu_max_percent = sample->cpu.total_usage_percent;
        }
        if (sample->cpu.total_usage_percent >= 85.0) {
            acc->cpu_hot_samples++;
        }
    }

    if (sample->memory_ok) {
        if (acc->memory_ok) {
            /* Keep latest totals; average used_percent. */
            acc->memory.used_percent =
                ((acc->memory.used_percent * (double)acc->memory_ok_samples) +
                 sample->memory.used_percent) /
                (double)(acc->memory_ok_samples + 1);
            acc->memory.available_physical_bytes =
                sample->memory.available_physical_bytes;
            acc->memory.used_physical_bytes = sample->memory.used_physical_bytes;
            acc->memory.total_physical_bytes = sample->memory.total_physical_bytes;
            /* Latest commit / fault snapshot (do not average cumulative faults). */
            acc->memory.commit_ok = sample->memory.commit_ok;
            acc->memory.commit_total_bytes = sample->memory.commit_total_bytes;
            acc->memory.commit_limit_bytes = sample->memory.commit_limit_bytes;
            acc->memory.commit_peak_bytes = sample->memory.commit_peak_bytes;
            acc->memory.commit_percent = sample->memory.commit_percent;
            acc->memory.pagefile_ok = sample->memory.pagefile_ok;
            acc->memory.commit_available_bytes =
                sample->memory.commit_available_bytes;
            acc->memory.hard_faults_ok = sample->memory.hard_faults_ok;
            acc->memory.hard_faults_per_sec = sample->memory.hard_faults_per_sec;
            acc->memory.page_faults_ok = sample->memory.page_faults_ok;
            acc->memory.page_faults_per_sec = sample->memory.page_faults_per_sec;
            acc->memory.standby_ok = sample->memory.standby_ok;
            acc->memory.standby_bytes = sample->memory.standby_bytes;
            acc->memory.modified_ok = sample->memory.modified_ok;
            acc->memory.modified_bytes = sample->memory.modified_bytes;
            acc->memory.compression_ok = sample->memory.compression_ok;
            acc->memory.compression_bytes = sample->memory.compression_bytes;
        } else {
            acc->memory = sample->memory;
            acc->memory_ok = 1;
        }
        acc->memory_ok_samples++;
        if (acc->memory_max_used_percent < 0.0 ||
            sample->memory.used_percent > acc->memory_max_used_percent) {
            acc->memory_max_used_percent = sample->memory.used_percent;
        }
        if (sample->memory.total_physical_bytes > 0) {
            double avail = (double)sample->memory.available_physical_bytes *
                           100.0 /
                           (double)sample->memory.total_physical_bytes;
            if (avail < 20.0) {
                acc->memory_pressure_samples++;
            }
        }
    }

    if (sample->disk_active_ok) {
        if (acc->disk_active_ok) {
            acc->disk_active_percent =
                ((acc->disk_active_percent * (double)acc->disk_active_ok_samples) +
                 sample->disk_active_percent) /
                (double)(acc->disk_active_ok_samples + 1);
        } else {
            acc->disk_active_percent = sample->disk_active_percent;
            acc->disk_active_ok = 1;
        }
        acc->disk_active_ok_samples++;
        if (acc->disk_active_max_percent < 0.0 ||
            sample->disk_active_percent > acc->disk_active_max_percent) {
            acc->disk_active_max_percent = sample->disk_active_percent;
        }
        if (sample->disk_active_percent >= 90.0) {
            acc->disk_active_hot_samples++;
        }
    }

    if (sample->disk_throughput_ok && acc->disk_throughput_ok) {
        acc->disk_read_bytes_per_sec =
            ((acc->disk_read_bytes_per_sec * (double)sample_index) +
             sample->disk_read_bytes_per_sec) /
            (double)(sample_index + 1);
        acc->disk_write_bytes_per_sec =
            ((acc->disk_write_bytes_per_sec * (double)sample_index) +
             sample->disk_write_bytes_per_sec) /
            (double)(sample_index + 1);
    }

    if (sample->disk_queue_ok && acc->disk_queue_ok) {
        acc->disk_avg_queue_length =
            ((acc->disk_avg_queue_length * (double)sample_index) +
             sample->disk_avg_queue_length) /
            (double)(sample_index + 1);
    }

    if (sample->disk_ok && acc->disk_ok &&
        sample->volume_count == acc->volume_count) {
        for (size_t i = 0; i < acc->volume_count; ++i) {
            WT_DiskVolumeMetrics *a = &acc->volumes[i];
            const WT_DiskVolumeMetrics *s = &sample->volumes[i];
            if (a->activity_ok && s->activity_ok) {
                a->active_percent =
                    ((a->active_percent * (double)sample_index) +
                     s->active_percent) /
                    (double)(sample_index + 1);
            }
            if (a->throughput_ok && s->throughput_ok) {
                a->read_bytes_per_sec =
                    ((a->read_bytes_per_sec * (double)sample_index) +
                     s->read_bytes_per_sec) /
                    (double)(sample_index + 1);
                a->write_bytes_per_sec =
                    ((a->write_bytes_per_sec * (double)sample_index) +
                     s->write_bytes_per_sec) /
                    (double)(sample_index + 1);
            }
            if (a->queue_ok && s->queue_ok) {
                a->avg_queue_length =
                    ((a->avg_queue_length * (double)sample_index) +
                     s->avg_queue_length) /
                    (double)(sample_index + 1);
            }
            if (s->read_latency_count > 0) {
                wt_disk_latency_push(a->read_latency_samples,
                                     &a->read_latency_count, WT_DISK_LATENCY_CAP,
                                     s->read_latency_samples[0],
                                     &a->read_latency_ms_p50,
                                     &a->read_latency_ms_p95);
            }
            if (s->write_latency_count > 0) {
                wt_disk_latency_push(a->write_latency_samples,
                                     &a->write_latency_count, WT_DISK_LATENCY_CAP,
                                     s->write_latency_samples[0],
                                     &a->write_latency_ms_p50,
                                     &a->write_latency_ms_p95);
            }
        }
    }

    if (sample->disk_read_latency_count > 0) {
        wt_disk_latency_push(acc->disk_read_latency_ms,
                             &acc->disk_read_latency_count, WT_DISK_LATENCY_CAP,
                             sample->disk_read_latency_ms[0],
                             &acc->disk_read_latency_ms_p50,
                             &acc->disk_read_latency_ms_p95);
    }
    if (sample->disk_write_latency_count > 0) {
        wt_disk_latency_push(acc->disk_write_latency_ms,
                             &acc->disk_write_latency_count, WT_DISK_LATENCY_CAP,
                             sample->disk_write_latency_ms[0],
                             &acc->disk_write_latency_ms_p50,
                             &acc->disk_write_latency_ms_p95);
    }
    acc->disk_latency_ok = (acc->disk_read_latency_count > 0 ||
                            acc->disk_write_latency_count > 0) ? 1 : 0;

    if (sample_index > 0 && sample->audio_ok) {
        acc->audio = sample->audio;
        acc->audio_ok = 1;
    }
    if (sample_index > 0 && sample->search_ok) {
        acc->search = sample->search;
        acc->search_ok = 1;
    }
    if (sample_index > 0 && sample->defender_ok) {
        acc->defender = sample->defender;
        acc->defender_ok = 1;
    }
    if (sample_index > 0 && sample->network_ok) {
        if (acc->network_ok) {
            wt_net_merge_sample(&acc->network, &sample->network, sample_index);
        } else {
            acc->network = sample->network;
        }
        acc->network_ok = 1;
    }

    (void)sample_total;
}

static WT_Result wt_run_scan_once(const WT_ScanOptions *opts,
                                  WT_ScanReport *report)
{
    unsigned int sample_ms = WT_SCAN_DEFAULT_SAMPLE_MS;
    size_t top_limit = WT_SCAN_DEFAULT_TOP_LIMIT;
    if (opts != NULL) {
        if (opts->cpu_sample_ms > 0) {
            sample_ms = opts->cpu_sample_ms;
        }
        if (opts->top_limit > 0) {
            top_limit = opts->top_limit;
        }
    }

    report->os_ok = (wt_collect_os_info(&report->os) == WT_OK);
    report->memory_ok = (wt_collect_memory_metrics(&report->memory) == WT_OK);
    {
        WT_NetTotals net_before;
        WT_NetTotals net_after;
        ULONGLONG net_t0 = GetTickCount64();
        int net_before_ok = (wt_collect_net_totals(&net_before) == WT_OK);
        report->cpu_ok = (wt_collect_cpu_metrics(sample_ms, &report->cpu) == WT_OK);
        if (net_before_ok && wt_collect_net_totals(&net_after) == WT_OK) {
            unsigned int elapsed = (unsigned int)(GetTickCount64() - net_t0);
            wt_net_apply_rates(&net_before, &net_after, elapsed, &report->network);
            report->network_ok = 1;
        } else {
            report->network_ok = 0;
        }
    }

    report->cpu_ok_samples = (report->cpu_ok && report->cpu.available) ? 1u : 0u;
    report->cpu_max_percent =
        (report->cpu_ok && report->cpu.available) ? report->cpu.total_usage_percent
                                                  : -1.0;
    report->cpu_hot_samples =
        (report->cpu_ok && report->cpu.available &&
         report->cpu.total_usage_percent >= 85.0)
            ? 1u
            : 0u;
    report->memory_ok_samples = report->memory_ok ? 1u : 0u;
    report->memory_max_used_percent =
        report->memory_ok ? report->memory.used_percent : -1.0;
    report->memory_pressure_samples = 0;
    if (report->memory_ok && report->memory.total_physical_bytes > 0) {
        double avail = (double)report->memory.available_physical_bytes * 100.0 /
                       (double)report->memory.total_physical_bytes;
        if (avail < 20.0) {
            report->memory_pressure_samples = 1;
        }
    }

    report->disk_ok =
        (wt_collect_disk_volumes(report->volumes, WT_MAX_VOLUMES,
                                 &report->volume_count) == WT_OK);

    WT_DiskIoMetrics disk_io;
    ZeroMemory(&disk_io, sizeof(disk_io));
    disk_io.avg_queue_length = -1.0;
    if (wt_collect_disk_io_ex(sample_ms, &disk_io, report->volumes,
                              report->disk_ok ? report->volume_count : 0) ==
        WT_OK) {
        report->disk_active_ok = disk_io.active_ok;
        report->disk_active_percent = disk_io.active_percent;
        report->disk_active_max_percent =
            disk_io.active_ok ? disk_io.active_percent : -1.0;
        report->disk_active_ok_samples = disk_io.active_ok ? 1u : 0u;
        report->disk_active_hot_samples =
            (disk_io.active_ok && disk_io.active_percent >= 90.0) ? 1u : 0u;
        report->disk_throughput_ok = disk_io.throughput_ok;
        report->disk_read_bytes_per_sec =
            disk_io.throughput_ok ? disk_io.read_bytes_per_sec : -1.0;
        report->disk_write_bytes_per_sec =
            disk_io.throughput_ok ? disk_io.write_bytes_per_sec : -1.0;
        report->disk_queue_ok = disk_io.queue_ok;
        report->disk_avg_queue_length =
            disk_io.queue_ok ? disk_io.avg_queue_length : -1.0;
        if (disk_io.read_latency_ms >= 0.0) {
            wt_disk_latency_push(report->disk_read_latency_ms,
                                 &report->disk_read_latency_count,
                                 WT_DISK_LATENCY_CAP, disk_io.read_latency_ms,
                                 &report->disk_read_latency_ms_p50,
                                 &report->disk_read_latency_ms_p95);
        }
        if (disk_io.write_latency_ms >= 0.0) {
            wt_disk_latency_push(report->disk_write_latency_ms,
                                 &report->disk_write_latency_count,
                                 WT_DISK_LATENCY_CAP, disk_io.write_latency_ms,
                                 &report->disk_write_latency_ms_p50,
                                 &report->disk_write_latency_ms_p95);
        }
        report->disk_latency_ok = (report->disk_read_latency_count > 0 ||
                                   report->disk_write_latency_count > 0)
                                      ? 1
                                      : 0;
    } else {
        report->disk_active_ok =
            (wt_collect_disk_activity(sample_ms, &report->disk_active_percent) ==
             WT_OK);
        report->disk_active_max_percent =
            report->disk_active_ok ? report->disk_active_percent : -1.0;
        report->disk_active_ok_samples = report->disk_active_ok ? 1u : 0u;
        report->disk_active_hot_samples =
            (report->disk_active_ok && report->disk_active_percent >= 90.0)
                ? 1u
                : 0u;
        report->disk_throughput_ok = 0;
        report->disk_queue_ok = 0;
        report->disk_read_bytes_per_sec = -1.0;
        report->disk_write_bytes_per_sec = -1.0;
        report->disk_avg_queue_length = -1.0;
        report->disk_latency_ok = 0;
        report->disk_read_latency_ms_p50 = -1.0;
        report->disk_read_latency_ms_p95 = -1.0;
        report->disk_write_latency_ms_p50 = -1.0;
        report->disk_write_latency_ms_p95 = -1.0;
    }
    report->power_ok = (wt_collect_power_info(&report->power) == WT_OK);

    report->gpu_ok = (wt_collect_gpu_metrics(sample_ms, &report->gpu) == WT_OK);
    report->audio_ok = (wt_collect_audio(&report->audio) == WT_OK);
    report->search_ok = (wt_collect_search(&report->search) == WT_OK);
    report->defender_ok = (wt_collect_defender(&report->defender) == WT_OK);

    WT_Result boot_r = wt_collect_boot_report(&report->boot, NULL);
    report->boot_ok = (boot_r == WT_OK);
    if (boot_r != WT_OK && boot_r != WT_ERR_NOT_FOUND) {
        WT_LOGD("boot metrics unavailable (%s)", wt_result_to_string(boot_r));
    }

    WT_Result upd_r = wt_collect_update_status_fast(&report->updates);
    report->updates_ok = (upd_r == WT_OK);

    wt_scan_collect_top_processes(report, top_limit, sample_ms);

    return WT_OK;
}

WT_Result wt_run_scan(const WT_ScanOptions *opts, WT_ScanReport *report)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_scan_report_init(report);

    unsigned int sample_count = WT_SCAN_DEFAULT_SAMPLE_COUNT;
    unsigned int sample_interval_ms = WT_SCAN_DEFAULT_SAMPLE_INTERVAL_MS;
    if (opts != NULL) {
        if (opts->sample_count > 0) {
            sample_count = opts->sample_count;
        }
        if (opts->sample_interval_ms > 0) {
            sample_interval_ms = opts->sample_interval_ms;
        }
    }
    if (sample_count > WT_SCAN_MAX_SAMPLES) {
        sample_count = WT_SCAN_MAX_SAMPLES;
    }
    if (sample_count == 0) {
        sample_count = WT_SCAN_DEFAULT_SAMPLE_COUNT;
    }

    report->scan_sample_count = sample_count;
    report->scan_sample_interval_ms = sample_interval_ms;

    WT_ScanReport sample;
    for (unsigned int i = 0; i < sample_count; ++i) {
        wt_scan_report_init(&sample);
        WT_Result r = wt_run_scan_once(opts, &sample);
        if (r != WT_OK) {
            return r;
        }
        wt_scan_merge_sample(report, &sample, i, sample_count);
        if (i + 1 < sample_count) {
            Sleep(sample_interval_ms);
        }
    }

    return WT_OK;
}
