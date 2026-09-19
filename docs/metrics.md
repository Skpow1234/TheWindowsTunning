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
  disk active time across samples (up to 32). Disk merge also tracks peak
  active % and how many samples were “hot” (>= 90%) so `WT-DISK-001` requires
  sustained pressure, not one spike (Phase 30).
- Recommendations are never derived from one noisy sample when multi-sample
  mode is used.
- Phase 35 confidence engine scores recommendations from sample count, scan
  window duration, and peak−average spread; mild single-sample CPU/GPU/disk
  alarms are suppressed (extreme ≥ 95% still allowed with lower confidence).
  Each recommendation includes `confidence_basis` in text/JSON.

### Phase 15 commands

```bash
wintune scan --samples 5 --interval 1000
wintune top --sort cpu
wintune top --sort disk
wintune top --sort memory
wintune top --sort network
wintune top --include-network
```

Per-process metrics in `top`, `scan`, and TUI:

- **CPU%** — PDH `\Process(*)\% Processor Time` when available; otherwise
  `GetProcessTimes` delta over the sample window (same approach as Task Manager)
- **Disk read/write rates** — delta of `GetProcessIoCounters` over the sample window
- **Network send/recv rates** — opt-in TCP Extended Stats (`--include-network`);
  see Network section (no payload capture; UDP not included)

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
\PhysicalDisk(_Total)\Avg. Disk Queue Length
\LogicalDisk(C:)\% Disk Time
\LogicalDisk(C:)\Disk Read Bytes/sec
\LogicalDisk(C:)\Disk Write Bytes/sec
\LogicalDisk(C:)\Avg. Disk Queue Length
```

**Collect:**
- Disk free space per volume.
- Disk active time (`% Disk Time`) system-wide and per fixed volume.
- Disk read/write bytes per second (`PhysicalDisk(_Total)` and per-volume
  `LogicalDisk`, Phases 25–26).
- Avg. disk queue length when available (total and per volume).
- Top disk-heavy processes where practical (per-process I/O deltas).

One PDH sample window (`wt_collect_disk_io_ex`) gathers PhysicalDisk(_Total)
and LogicalDisk counters for each fixed volume so scan does not pay the sample
interval twice.

WinTune never deletes user files, cleans temporary files (v1), or manually
removes WinSxS / System32 / Windows Update cache / browser cache / app data.

```c
typedef struct WT_DiskVolumeMetrics {
    wchar_t root_path[16];
    unsigned long long total_bytes;
    unsigned long long free_bytes;
    double free_percent;
    double active_percent;       /* LogicalDisk; -1 / flags when n/a */
    double read_bytes_per_sec;
    double write_bytes_per_sec;
    double avg_queue_length;
    int activity_ok;
    int throughput_ok;
    int queue_ok;
} WT_DiskVolumeMetrics;

typedef struct WT_DiskIoMetrics {
    double active_percent;
    double read_bytes_per_sec;
    double write_bytes_per_sec;
    double avg_queue_length; /* -1 if unavailable */
    int active_ok;
    int throughput_ok;
    int queue_ok;
} WT_DiskIoMetrics;
```

Scan JSON `disk` object includes system totals (`read_bytes_per_sec`,
`write_bytes_per_sec`, `avg_queue_length`, `*_available` /
`throughput_available`) and each volume may include the same activity fields
(`active_percent`, throughput, queue) when LogicalDisk counters succeed.

Multi-sample scans (Phase 30) also expose:
- `active_percent` — average across successful samples
- `active_max_percent` — peak sample
- `active_ok_samples` / `active_hot_samples` — count of successful samples and
  how many were >= 90% active

Recommendations:
- `WT-DISK-001` — sustained system-wide active pressure (average >= 90% or a
  majority of samples hot); cites hottest volume and sample counts when known.
  A single spike among otherwise cool samples does not fire.
- `WT-DISK-002` — low free space on a volume.
- `WT-DISK-003` — high throughput without extreme active %.
- `WT-DISK-004` — one volume’s LogicalDisk active time is high while
  PhysicalDisk(_Total) is not (localized pressure).

## Storage health (Phase 48)

Read-only reliability signals via Windows storage APIs (not PDH):

```text
\\.\PhysicalDriveN
IOCTL_STORAGE_QUERY_PROPERTY   (identity / bus)
IOCTL_STORAGE_PREDICT_FAILURE  (failure prediction when supported)
```

Command: `wintune storage` / `wintune storage --json`.

- Reports model, serial, bus, SSD/HDD hint, and predict-failure status.
- Some NVMe/USB devices return “not supported” — that is normal.
- **Never** wipe, format, or repair disks.

Recommendation:

- `WT-DISK-005` — failure prediction reported → back up + vendor diagnostics
  (advisory only).

## Reliability / WER signals (Phase 49)

Read-only Event Log + dump **metadata** (never dump contents):

```text
System:      EventID 41 (Kernel-Power), 6008 (unexpected shutdown), 1001 (bugcheck)
Application: EventID 1000 (crash), 1001 (WER), 1002 (hang)
Folders:     %SystemRoot%\Minidump, MEMORY.DMP, WER ReportArchive/Queue (names/sizes)
```

Command: `wintune reliability` / `--json`.

Recommendations (advisory):

- `WT-RELIABILITY-001` — unexpected shutdowns
- `WT-RELIABILITY-002` — repeated app crashes/hangs
- `WT-RELIABILITY-003` — bugcheck / kernel dump metadata

**Never:** open/upload dumps; claim to repair corruption; clear event logs.

---

## Network

**APIs / counters:**

```c
GetIfTable                         /* system-wide interface octets (TUI) */
GetExtendedTcpTable                /* TCP table with owning PID */
SetPerTcpConnectionEStats /
GetPerTcpConnectionEStats          /* opt-in per-process TCP byte rates */
SetPerTcp6ConnectionEStats /
GetPerTcp6ConnectionEStats
```

**Collect:**
- System-wide received/sent octets across non-loopback interfaces (`wt_collect_net_totals`).
- Opt-in per-process TCP send/recv bytes/sec (`--include-network` / TUI `w`)
  via IP Helper Extended Stats aggregated by OwningPid (Phase 27).

**Overhead:** Per-process sampling enables TCP EStats on up to ~1024 connections
for one sample window. Keep it off unless you need Net columns or
`--sort network`. No packet capture of payloads; UDP is not included.

Sensitive network details are not exposed by default. WinTune does not change
DNS, proxy, firewall, routing, or adapter settings in v1.

---

## GPU / Display (Phase 28)

**APIs / counters:**

```c
CreateDXGIFactory1 / IDXGIAdapter1::GetDesc1   /* hardware adapters */
EnumDisplayDevicesW / GetSystemMetrics         /* display count + primary size */
\GPU Engine(*)\Utilization Percentage          /* PDH; aggregate by LUID */
```

**Collect:**
- Hardware GPU name, dedicated and shared video memory (software/WARP skipped).
- Per-adapter engine busy % (max across engine types for that LUID, clamped
  0–100%). Soft-fails when the counter set is missing (some VMs).
- Active desktop display count and primary resolution.

Read-only only. WinTune never installs drivers, overclocks, undervolts, or
claims FPS “boosts.” Advisory `WT-GPU-001` may note very high engine busy.

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
- For top-process lists: publisher (`CompanyName`), product (`ProductName`),
  Authenticode status, origin cue, install-location class, and
  `unusual_location` (calm review only) via `src/system/file_identity.c`.
  Path results are cached for the process lifetime.

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
    WT_FileIdentity identity; /* product, publisher, location, signature */
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

**Per item:** id, name, source, command/path, product name, publisher,
signature status, origin, install location, unusual-location review flag,
enabled state, `impact` band, `impact_score` (0–100), `impact_confidence`.
Unsigned alone is never treated as malware; unusual location is a calm review
cue only.

### Impact scoring v2

Per-item impact (startup / task), **not** a whole-PC health percentage.

Implementation: `src/core/impact_score.c`.

| Evidence | Points (additive, then clamp 0–100) |
| -------- | ----------------------------------- |
| Measured boot delay ≥ 10s | +60 |
| Measured 3–10s | +45 |
| Measured 1–3s | +25 |
| Measured > 0 | +10 |
| Known heavy name heuristic | +20 (+8 if measured already present) |
| Third-party publisher | +5 |
| Microsoft / protected | −15 |
| Unusual install location | +10 |
| Matched process CPU ≥ 15% / ≥ 5% | +20 / +10 |
| Working set ≥ 500 MB / ≥ 200 MB | +20 / +10 |
| Disk I/O ≥ 5 MB/s / ≥ 1 MB/s | +15 / +10 |

**Bands:** unknown (no evidence); low (1–34); medium (35–64); high (≥ 65).

**Confidence:** measured +50, heuristic +10, publisher +10, location +10,
runtime +20 (clamp 0–95).

**Recommend disable/delay** when `impact_score ≥ 65` and
`impact_confidence ≥ 50`.

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

**Collect:** service name, display name, status, startup type, configured
ImagePath, PID if running, publisher / Authenticode / origin metadata
(best-effort; path-cached).

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
PowerReadACValueIndex / PowerReadDCValueIndex  /* PROCTHROTTLEMAX */
GetSystemPowerStatus
CallNtPowerInformation(SystemBatteryState)     /* rate mW, charge state */
```

**Collect:** AC/battery status, current active power scheme, battery percentage,
charging/discharging, signed discharge rate (mW; negative while draining),
estimated remaining time when available, and the active plan’s processor
maximum state for AC and DC (Phase 29). A “capped” flag is set when the current
source’s max is below 100%.

Recommendations use existing Windows plans only (`WT-POWER-001`/`002` apply;
`WT-POWER-003`/`004` are advisory). WinTune never edits fan curves or firmware.

The previous scheme is preserved as rollback metadata before any plan change.

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
