#ifndef WINTUNE_TRAY_H
#define WINTUNE_TRAY_H

#include "common/error.h"

/* Runs the Win32 system-tray message loop until the user exits.
 * Detaches from any console. Returns WT_OK on clean exit. */
WT_Result wt_tray_run(void);

#endif /* WINTUNE_TRAY_H */
