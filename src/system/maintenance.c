#include "system/maintenance.h"

#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <taskschd.h>
#include <strsafe.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#define WT_MAINT_LAST_RUN_MINUTES 45

typedef struct WT_MaintProcessRule {
    const wchar_t *name;
    WT_MaintKind kind;
} WT_MaintProcessRule;

typedef struct WT_MaintTaskRule {
    const wchar_t *folder; /* e.g. \Microsoft\Windows\Windows Defender */
    const wchar_t *name;
    WT_MaintKind kind;
} WT_MaintTaskRule;

static const WT_MaintProcessRule g_proc_rules[] = {
    { L"MsMpEng.exe", WT_MAINT_DEFENDER },
    { L"MpCmdRun.exe", WT_MAINT_DEFENDER },
    { L"NisSrv.exe", WT_MAINT_DEFENDER },
    { L"SecurityHealthService.exe", WT_MAINT_DEFENDER },
    { L"TiWorker.exe", WT_MAINT_WINDOWS_UPDATE },
    { L"TrustedInstaller.exe", WT_MAINT_WINDOWS_UPDATE },
    { L"UsoClient.exe", WT_MAINT_WINDOWS_UPDATE },
    { L"MoUsoCoreWorker.exe", WT_MAINT_WINDOWS_UPDATE },
    { L"wuauclt.exe", WT_MAINT_WINDOWS_UPDATE },
    { L"SearchIndexer.exe", WT_MAINT_OPTIMIZATION },
    { L"SearchProtocolHost.exe", WT_MAINT_OPTIMIZATION },
    { L"SearchFilterHost.exe", WT_MAINT_OPTIMIZATION },
    { L"CompatTelRunner.exe", WT_MAINT_OPTIMIZATION },
    { L"defrag.exe", WT_MAINT_OPTIMIZATION },
};

static const WT_MaintTaskRule g_task_rules[] = {
    { L"\\Microsoft\\Windows\\Windows Defender",
      L"Windows Defender Scheduled Scan", WT_MAINT_DEFENDER },
    { L"\\Microsoft\\Windows\\Windows Defender",
      L"Windows Defender Cache Maintenance", WT_MAINT_DEFENDER },
    { L"\\Microsoft\\Windows\\Windows Defender",
      L"Windows Defender Cleanup", WT_MAINT_DEFENDER },
    { L"\\Microsoft\\Windows\\UpdateOrchestrator", L"Schedule Scan",
      WT_MAINT_WINDOWS_UPDATE },
    { L"\\Microsoft\\Windows\\UpdateOrchestrator", L"USO_UxBroker",
      WT_MAINT_WINDOWS_UPDATE },
    { L"\\Microsoft\\Windows\\WindowsUpdate", L"Scheduled Start",
      WT_MAINT_WINDOWS_UPDATE },
    { L"\\Microsoft\\Windows\\Defrag", L"ScheduledDefrag",
      WT_MAINT_OPTIMIZATION },
    { L"\\Microsoft\\Windows\\Sysmain", L"WsSwapAssessmentTask",
      WT_MAINT_OPTIMIZATION },
    { L"\\Microsoft\\Windows\\Application Experience",
      L"Microsoft Compatibility Appraiser", WT_MAINT_OPTIMIZATION },
};

const char *wt_maint_kind_name(WT_MaintKind kind)
{
    switch (kind) {
    case WT_MAINT_DEFENDER:       return "defender";
    case WT_MAINT_WINDOWS_UPDATE: return "windows_update";
    case WT_MAINT_OPTIMIZATION:   return "optimization";
    default:                      return "unknown";
    }
}

void wt_maintenance_init(WT_MaintenanceReport *report)
{
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

static WT_MaintKind wt_maint_match_process(const wchar_t *name)
{
    size_t i;
    if (name == NULL || name[0] == L'\0') {
        return WT_MAINT_UNKNOWN;
    }
    for (i = 0; i < sizeof(g_proc_rules) / sizeof(g_proc_rules[0]); ++i) {
        if (_wcsicmp(name, g_proc_rules[i].name) == 0) {
            return g_proc_rules[i].kind;
        }
    }
    return WT_MAINT_UNKNOWN;
}

static void wt_maint_add_hit(WT_MaintenanceReport *r, const WT_MaintHit *hit)
{
    size_t i;
    if (r == NULL || hit == NULL || r->hit_count >= WT_MAX_MAINT_HITS) {
        return;
    }
    /* Dedup by kind+source+name */
    for (i = 0; i < r->hit_count; ++i) {
        if (r->hits[i].kind == hit->kind &&
            _wcsicmp(r->hits[i].source, hit->source) == 0 &&
            _wcsicmp(r->hits[i].name, hit->name) == 0) {
            if (hit->cpu_percent > r->hits[i].cpu_percent) {
                r->hits[i].cpu_percent = hit->cpu_percent;
            }
            if (hit->disk_bytes_per_sec > r->hits[i].disk_bytes_per_sec) {
                r->hits[i].disk_bytes_per_sec = hit->disk_bytes_per_sec;
            }
            if (hit->task_running) {
                r->hits[i].task_running = 1;
            }
            if (hit->last_run_recent) {
                r->hits[i].last_run_recent = 1;
                StringCchCopyA(r->hits[i].last_run_utc,
                               sizeof(r->hits[i].last_run_utc),
                               hit->last_run_utc);
            }
            return;
        }
    }
    r->hits[r->hit_count++] = *hit;
    if (hit->kind == WT_MAINT_DEFENDER) {
        r->defender_active = 1;
    } else if (hit->kind == WT_MAINT_WINDOWS_UPDATE) {
        r->update_active = 1;
    } else if (hit->kind == WT_MAINT_OPTIMIZATION) {
        r->optimize_active = 1;
    }
}

static void wt_maint_from_processes(WT_MaintenanceReport *r,
                                    const WT_ScanReport *scan)
{
    size_t i;
    if (scan == NULL) {
        return;
    }
    for (i = 0; i < scan->top_process_count; ++i) {
        const WT_ProcessInfo *p = &scan->top_processes[i];
        WT_MaintKind kind = wt_maint_match_process(p->name);
        WT_MaintHit hit;
        double disk;
        if (kind == WT_MAINT_UNKNOWN) {
            continue;
        }
        memset(&hit, 0, sizeof(hit));
        hit.kind = kind;
        StringCchCopyW(hit.source, ARRAYSIZE(hit.source), L"process");
        StringCchCopyW(hit.name, ARRAYSIZE(hit.name), p->name);
        hit.cpu_percent = p->cpu_percent;
        disk = 0.0;
        if (p->disk_read_bytes_per_sec > 0.0) {
            disk += p->disk_read_bytes_per_sec;
        }
        if (p->disk_write_bytes_per_sec > 0.0) {
            disk += p->disk_write_bytes_per_sec;
        }
        hit.disk_bytes_per_sec = (disk > 0.0) ? disk : -1.0;
        wt_maint_add_hit(r, &hit);
    }
}

static int wt_maint_date_to_utc(DATE dt, char *out, size_t count)
{
    SYSTEMTIME st;
    FILETIME ft;
    out[0] = '\0';
    if (dt == 0.0) {
        return 0;
    }
    if (!VariantTimeToSystemTime(dt, &st)) {
        return 0;
    }
    if (!SystemTimeToFileTime(&st, &ft)) {
        return 0;
    }
    /* Task Scheduler LastRunTime is local; convert to UTC for display. */
    {
        FILETIME utc;
        SYSTEMTIME stu;
        if (!LocalFileTimeToFileTime(&ft, &utc)) {
            utc = ft;
        }
        if (!FileTimeToSystemTime(&utc, &stu)) {
            return 0;
        }
        snprintf(out, count, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                 (unsigned)stu.wYear, (unsigned)stu.wMonth, (unsigned)stu.wDay,
                 (unsigned)stu.wHour, (unsigned)stu.wMinute,
                 (unsigned)stu.wSecond);
    }
    return 1;
}

static int wt_maint_last_run_recent(DATE dt)
{
    SYSTEMTIME st;
    FILETIME ft;
    FILETIME now;
    ULARGE_INTEGER a;
    ULARGE_INTEGER b;
    ULONGLONG diff_min;

    if (dt == 0.0) {
        return 0;
    }
    if (!VariantTimeToSystemTime(dt, &st) || !SystemTimeToFileTime(&st, &ft)) {
        return 0;
    }
    GetSystemTimeAsFileTime(&now);
    /* Compare as local vs UTC loosely: also try LocalFileTimeToFileTime. */
    {
        FILETIME local_as_utc;
        if (LocalFileTimeToFileTime(&ft, &local_as_utc)) {
            ft = local_as_utc;
        }
    }
    a.LowPart = ft.dwLowDateTime;
    a.HighPart = ft.dwHighDateTime;
    b.LowPart = now.dwLowDateTime;
    b.HighPart = now.dwHighDateTime;
    if (b.QuadPart < a.QuadPart) {
        return 1; /* clock skew — treat as recent */
    }
    diff_min = (b.QuadPart - a.QuadPart) / 10000000ull / 60ull;
    return diff_min <= (ULONGLONG)WT_MAINT_LAST_RUN_MINUTES;
}

static void wt_maint_probe_tasks(WT_MaintenanceReport *r)
{
    ITaskService *service = NULL;
    HRESULT hr;
    VARIANT empty;
    size_t i;
    int need_uninit = 0;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        need_uninit = 1;
    } else if (hr != RPC_E_CHANGED_MODE) {
        WT_LOGW("maintenance CoInitializeEx failed (hr=0x%08lx)", (long)hr);
        return;
    }

    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          &IID_ITaskService, (void **)&service);
    if (FAILED(hr) || service == NULL) {
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }

    VariantInit(&empty);
    empty.vt = VT_EMPTY;
    hr = service->lpVtbl->Connect(service, empty, empty, empty, empty);
    if (FAILED(hr)) {
        service->lpVtbl->Release(service);
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }

    for (i = 0; i < sizeof(g_task_rules) / sizeof(g_task_rules[0]); ++i) {
        ITaskFolder *folder = NULL;
        IRegisteredTask *task = NULL;
        BSTR bfolder;
        BSTR bname;
        TASK_STATE state = TASK_STATE_UNKNOWN;
        DATE last = 0.0;
        WT_MaintHit hit;
        int interesting = 0;

        bfolder = SysAllocString(g_task_rules[i].folder);
        bname = SysAllocString(g_task_rules[i].name);
        if (bfolder == NULL || bname == NULL) {
            SysFreeString(bfolder);
            SysFreeString(bname);
            continue;
        }
        hr = service->lpVtbl->GetFolder(service, bfolder, &folder);
        SysFreeString(bfolder);
        if (FAILED(hr) || folder == NULL) {
            SysFreeString(bname);
            continue;
        }
        hr = folder->lpVtbl->GetTask(folder, bname, &task);
        SysFreeString(bname);
        if (FAILED(hr) || task == NULL) {
            folder->lpVtbl->Release(folder);
            continue;
        }

        (void)task->lpVtbl->get_State(task, &state);
        (void)task->lpVtbl->get_LastRunTime(task, &last);

        memset(&hit, 0, sizeof(hit));
        hit.kind = g_task_rules[i].kind;
        StringCchCopyW(hit.source, ARRAYSIZE(hit.source), L"task");
        StringCchPrintfW(hit.name, ARRAYSIZE(hit.name), L"%ls\\%ls",
                         g_task_rules[i].folder, g_task_rules[i].name);
        hit.cpu_percent = -1.0;
        hit.disk_bytes_per_sec = -1.0;
        if (state == TASK_STATE_RUNNING) {
            hit.task_running = 1;
            interesting = 1;
        }
        if (wt_maint_last_run_recent(last)) {
            hit.last_run_recent = 1;
            wt_maint_date_to_utc(last, hit.last_run_utc, sizeof(hit.last_run_utc));
            interesting = 1;
        }
        if (interesting) {
            wt_maint_add_hit(r, &hit);
        }

        task->lpVtbl->Release(task);
        folder->lpVtbl->Release(folder);
    }

    service->lpVtbl->Release(service);
    if (need_uninit) {
        CoUninitialize();
    }
}

static void wt_maint_finalize(WT_MaintenanceReport *r)
{
    r->overlap = (r->cpu_hot || r->disk_hot) &&
                 (r->defender_active || r->update_active || r->optimize_active);

    if (r->overlap) {
        StringCchCopyW(r->note, ARRAYSIZE(r->note),
                       L"Maintenance activity overlaps high CPU/disk samples. "
                       L"Prefer scheduling scans/updates/optimize outside work "
                       L"hours. WinTune never disables Defender or Windows "
                       L"Update.");
    } else if (r->defender_active || r->update_active || r->optimize_active) {
        StringCchCopyW(r->note, ARRAYSIZE(r->note),
                       L"Maintenance-related processes or tasks were visible, "
                       L"but samples were not sustained-hot. Still prefer "
                       L"off-hours scheduling when practical.");
    } else {
        StringCchCopyW(r->note, ARRAYSIZE(r->note),
                       L"No overlapping Defender/WU/optimization activity "
                       L"detected during this sample window.");
    }
}

WT_Result wt_maintenance_from_scan(WT_MaintenanceReport *report,
                                   const WT_ScanReport *scan)
{
    if (report == NULL) {
        return WT_ERR_INVALID_ARGUMENT;
    }
    wt_maintenance_init(report);

    if (scan != NULL) {
        report->scan_ok = 1;
        report->sample_count = scan->scan_sample_count > 0
                                   ? scan->scan_sample_count
                                   : 1u;
        if (scan->cpu_ok && scan->cpu.available) {
            report->cpu_percent = scan->cpu.total_usage_percent;
        }
        if (scan->disk_active_ok) {
            report->disk_active_percent = scan->disk_active_percent;
        }
        report->cpu_hot_samples = scan->cpu_hot_samples;
        report->disk_hot_samples = scan->disk_active_hot_samples;

        if (scan->cpu_ok_samples > 1u) {
            report->cpu_hot =
                ((scan->cpu_hot_samples * 2u) > scan->cpu_ok_samples) ||
                (scan->cpu.total_usage_percent >= 85.0);
        } else {
            report->cpu_hot = (scan->cpu_ok && scan->cpu.available &&
                               scan->cpu.total_usage_percent >= 85.0);
        }
        if (scan->disk_active_ok_samples > 1u) {
            report->disk_hot =
                ((scan->disk_active_hot_samples * 2u) >
                 scan->disk_active_ok_samples) ||
                (scan->disk_active_percent >= 90.0);
        } else {
            report->disk_hot =
                (scan->disk_active_ok && scan->disk_active_percent >= 90.0);
        }

        wt_maint_from_processes(report, scan);
    }

    wt_maint_probe_tasks(report);
    wt_maint_finalize(report);
    return WT_OK;
}
