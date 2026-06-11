#include "output/table.h"
#include "common/units.h"

#include <stdio.h>

void wt_print_process_table(const WT_ProcessInfo *items, size_t count)
{
    printf("%-8s %-28s %12s %12s\n", "PID", "Process", "Memory", "Private");

    for (size_t i = 0; i < count; ++i) {
        wchar_t memory[32];
        wchar_t private_bytes[32];
        wt_format_bytes(items[i].working_set_bytes, memory, 32);
        wt_format_bytes(items[i].private_bytes, private_bytes, 32);

        printf("%-8lu %-28.28ls %12ls %12ls\n",
               items[i].pid, items[i].name, memory, private_bytes);
    }
}
