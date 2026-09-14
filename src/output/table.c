#include "output/table.h"
#include "common/units.h"
#include "system/file_identity.h"

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
    int any_unusual = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].cpu_percent >= 0.0) {
            show_cpu = 1;
        }
        if (items[i].disk_read_bytes_per_sec >= 0.0 ||
                items[i].disk_write_bytes_per_sec >= 0.0) {
            show_disk = 1;
        }
        if (items[i].identity.unusual_location) {
            any_unusual = 1;
        }
    }

    if (show_cpu || show_disk) {
        printf("%-8s %-16s %-11s %-8s %7s %11s %11s %11s\n",
               "PID", "Process", "Origin", "Loc", "CPU%", "Memory", "Disk R",
               "Disk W");
    } else {
        printf("%-8s %-20s %-11s %-8s %12s %12s\n",
               "PID", "Process", "Origin", "Loc", "Memory", "Private");
    }

    for (size_t i = 0; i < count; ++i) {
        wchar_t memory[32];
        wchar_t private_bytes[32];
        wt_format_bytes(items[i].working_set_bytes, memory, 32);
        wt_format_bytes(items[i].private_bytes, private_bytes, 32);
        const char *origin = wt_publisher_origin_name(items[i].identity.origin);
        const char *loc = wt_install_location_name(items[i].identity.location);
        char loc_mark[16];
        if (items[i].identity.unusual_location) {
            snprintf(loc_mark, sizeof(loc_mark), "%s!", loc);
        } else {
            snprintf(loc_mark, sizeof(loc_mark), "%s", loc);
        }

        if (show_cpu || show_disk) {
            wchar_t disk_r[32];
            wchar_t disk_w[32];
            wt_format_rate(items[i].disk_read_bytes_per_sec, disk_r, 32);
            wt_format_rate(items[i].disk_write_bytes_per_sec, disk_w, 32);

            if (items[i].cpu_percent >= 0.0) {
                printf("%-8lu %-16.16ls %-11s %-8s %6.1f%% %11ls %11ls %11ls\n",
                       items[i].pid, items[i].name, origin, loc_mark,
                       items[i].cpu_percent, memory, disk_r, disk_w);
            } else {
                printf("%-8lu %-16.16ls %-11s %-8s %7s %11ls %11ls %11ls\n",
                       items[i].pid, items[i].name, origin, loc_mark, "-",
                       memory, disk_r, disk_w);
            }
        } else {
            printf("%-8lu %-20.20ls %-11s %-8s %12ls %12ls\n",
                   items[i].pid, items[i].name, origin, loc_mark, memory,
                   private_bytes);
        }
    }

    if (any_unusual) {
        printf("\nLoc marked with ! is worth a calm review (temp/downloads, or "
               "Microsoft-labeled binary outside Windows/Program Files). "
               "Not a malware claim.\n");
    }
}
