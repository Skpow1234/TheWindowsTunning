#ifndef WINTUNE_TABLE_H
#define WINTUNE_TABLE_H

#include <stddef.h>

#include "metrics/process.h"

/* Prints an aligned process table (PID, Process, Memory, Private) to stdout. */
void wt_print_process_table(const WT_ProcessInfo *items, size_t count);

#endif /* WINTUNE_TABLE_H */
