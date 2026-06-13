#include "output/table.h"
#include "common/units.h"

#include <stdio.h>
#include <strsafe.h>

static void wt_format_rate(double bytes_per_sec, wchar_t *out, size_t cap)
{
    if (bytes_per_sec < 0.0) {
        StringCchCopyW(out, cap, L"-");
        return;
    }
    wt_format_bytes((unsigned long long)bytes_per_sec, out, (int)cap);
    size_t len = wcslen(out);
    if (len + 4 < cap) {
        StringCchCatW(out, cap, L"/s");
    }
}

void wt_print_process_table(const WT_ProcessInfo *items, size_t count)
{
    int show_cpu = 0;
    int show_disk = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].cpu_percent >= 0.0) {
            show_cpu = 1;
        }
        if (items[i].disk_read_bytes_per_sec >= 0.0 ||
                items[i].disk_write_bytes_per_sec >= 0.0) {
            show_disk = 1;
        }
    }

    if (show_cpu || show_disk) {
        printf("%-8s %-22s %8s %12s %12s %12s\n",
               "PID", "Process", "CPU%", "Memory", "Disk R", "Disk W");
    } else {
        printf("%-8s %-28s %12s %12s\n", "PID", "Process", "Memory", "Private");
    }

    for (size_t i = 0; i < count; ++i) {
        wchar_t memory[32];
        wchar_t private_bytes[32];
        wt_format_bytes(items[i].working_set_bytes, memory, 32);
        wt_format_bytes(items[i].private_bytes, private_bytes, 32);

        if (show_cpu || show_disk) {
            wchar_t disk_r[32];
            wchar_t disk_w[32];
            wt_format_rate(items[i].disk_read_bytes_per_sec, disk_r, 32);
            wt_format_rate(items[i].disk_write_bytes_per_sec, disk_w, 32);

            if (items[i].cpu_percent >= 0.0) {
                printf("%-8lu %-22.22ls %7.1f%% %12ls %12ls %12ls\n",
                       items[i].pid, items[i].name, items[i].cpu_percent,
                       memory, disk_r, disk_w);
            } else {
                printf("%-8lu %-22.22ls %8s %12ls %12ls %12ls\n",
                       items[i].pid, items[i].name, "-", memory, disk_r, disk_w);
            }
        } else {
            printf("%-8lu %-28.28ls %12ls %12ls\n",
                   items[i].pid, items[i].name, memory, private_bytes);
        }
    }
}
