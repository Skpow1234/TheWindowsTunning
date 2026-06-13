# WinTune Metrics

WinTune collects metrics in layers using official Windows APIs. PDH is
preferred for stable high-level counters; Win32/PSAPI/Tool Help are used for
per-process detail. All API calls check their return values, and partial
failures are recorded rather than fatal.

---

## Sampling Model

- Prefer multiple samples over a short interval rather than a single noisy read.
- Default single-sample window: **500 ms** PDH interval for CPU and disk activity.
- Multi-sample scans: `wintune scan --samples N --interval MS` averages CPU and
  disk active time across samples (up to 32).
- Recommendations are never derived from one noisy sample when multi-sample
  mode is used.

### Phase 15 commands

```bash
wintune scan --samples 5 --interval 1000
wintune top --sort cpu
wintune top --sort disk
wintune top --sort memory
```

Per-process metrics in `top`, `scan`, and TUI:

- **CPU%** — PDH `\Process(*)\% Processor Time` mapped via `\Process(*)\ID Process`
- **Disk read/write rates** — delta of `GetProcessIoCounters` over the sample window
- Per-process network rates are not available without ETW (future phase)

---

## CPU

**APIs / counters:** PDH first.

```text
\Processor(_Total)\% Processor Time
\Process(*)\% Processor Time
```

**Collect:**
- Total CPU percentage.
- Per-process CPU estimate (normalized by logical processor count where needed).
- Number of logical processors.
- Processor name if easily available.
- Load trend over the sampling window.

---

## Memory

**APIs:**

```c
GlobalMemoryStatusEx
GetProcessMemoryInfo
```

**Collect:**
- Total physical memory.
- Available physical memory.
- Used physical memory and used percent.
- Per-process working set.
- Per-process private bytes if available.

Memory pressure recommendations use clear thresholds. WinTune never implements
"RAM cleaner" behavior and never force-empties working sets in v1.

```c
typedef struct WT_MemoryMetrics {
    unsigned long long total_physical_bytes;
    unsigned long long available_physical_bytes;
    unsigned long long used_physical_bytes;
    double used_percent;
} WT_MemoryMetrics;
```

---

## Disk

**APIs / counters:**

```c
GetDiskFreeSpaceExW
GetLogicalDrives
GetDriveTypeW
GetProcessIoCounters
```

```text
\PhysicalDisk(_Total)\% Disk Time
\PhysicalDisk(_Total)\Disk Read Bytes/sec
\PhysicalDisk(_Total)\Disk Write Bytes/sec
```

**Collect:**
- Disk free space per volume.
- Disk active time.
- Disk read/write bytes per second.
- Top disk-heavy processes where practical.

WinTune never deletes user files, cleans temporary files (v1), or manually
removes WinSxS / System32 / Windows Update cache / browser cache / app data.

```c
typedef struct WT_DiskVolumeMetrics {
    wchar_t root_path[16];
    unsigned long long total_bytes;
    unsigned long long free_bytes;
    double free_percent;
} WT_DiskVolumeMetrics;
```

---

## Network

**APIs / counters:**

```c
GetAdaptersAddresses
GetIfTable2
```

```text
\Network Interface(*)\Bytes Total/sec
```

**Collect:**
- Adapter names and operational status.
- Bytes sent/received per second.
- Basic connectivity indicators.
- Basic IP information if useful.

Sensitive network details are not exposed by default. WinTune does not change
DNS, proxy, firewall, routing, or adapter settings in v1.

---

## Processes

**APIs:**

```c
CreateToolhelp32Snapshot
Process32FirstW / Process32NextW
OpenProcess
GetProcessMemoryInfo
GetProcessIoCounters
GetProcessTimes
CloseHandle
```

**Collect:**
- PID and executable name.
- CPU estimate.
- Memory working set; private bytes if possible.
- I/O counters.
- Session ID.
- Is-elevated / is-system if possible.

Full command line is **not** collected by default (it may contain secrets). It
can be enabled behind `--include-command-line`, with a warning.

```c
typedef struct WT_ProcessInfo {
    unsigned long pid;
    wchar_t name[260];
    unsigned long long working_set_bytes;
    unsigned long long private_bytes;
    unsigned long long read_bytes;
    unsigned long long write_bytes;
    double cpu_percent;
} WT_ProcessInfo;
```

---

## Startup Entries

**Sources:**
- Registry Run keys (read-only):
  - `HKCU\...\CurrentVersion\Run`
  - `HKLM\...\CurrentVersion\Run`
  - `HKCU\...\CurrentVersion\RunOnce`
  - `HKLM\...\CurrentVersion\RunOnce`
- Startup folders:
  - `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`
  - `%PROGRAMDATA%\Microsoft\Windows\Start Menu\Programs\Startup`
- Auto-start services.
- Scheduled tasks (later phases).

**Per item:** id, name, source, path, publisher (later), enabled state,
estimated impact (heuristic in v1), risk.

---

## Services

**APIs:** Service Control Manager.

```c
OpenSCManagerW
EnumServicesStatusExW
OpenServiceW
QueryServiceStatusEx
QueryServiceConfigW
```

**Collect:** service name, display name, status, startup type, binary path,
service type, PID if running, CPU/memory if the PID can be mapped,
Microsoft-signed (later).

Microsoft services are not judged aggressively; third-party services may be
flagged for review only.

---

## Power

**APIs:**

```c
PowerGetActiveScheme
PowerSetActiveScheme
PowerEnumerate
PowerReadFriendlyName
GetSystemPowerStatus
```

**Collect:** AC/battery status, current active power scheme, available known
schemes, battery percentage if applicable, and a recommendation based on the
plugged-in state. The previous scheme is preserved as rollback metadata before
any change.

---

## The Scan Report Model

All metrics feed a single stable model consumed by text, JSON, and TUI outputs.

```c
typedef struct WT_ScanReport {
    WT_CpuMetrics cpu;
    WT_MemoryMetrics memory;
    WT_DiskVolumeMetrics volumes[32];
    size_t volume_count;
    WT_ProcessInfo top_processes[64];
    size_t top_process_count;
} WT_ScanReport;
```

Fixed-size arrays are acceptable in v1 where limits are documented.

---

## Performance Budget

```text
CLI cold start: under 500 ms when possible
Basic scan: under 10 seconds
CLI memory usage: under 50 MB
TUI refresh interval: default 1000 ms
```

Avoid heavy polling, avoid WMI in hot paths, and never let the monitor become
the bottleneck.
