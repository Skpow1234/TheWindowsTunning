#include "tray/tray_status.h"

#include "metrics/cpu.h"
#include "metrics/memory.h"
#include "platform/paths.h"
#include "platform/service_client.h"
#include "system/os_info.h"
#include "system/power.h"

#include "resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>

#define WT_TRAY_STATUS_CLASS L"WinTuneTrayStatus"
#define WT_ID_REFRESH 2001
#define WT_ID_CLOSE   2002

static double wt_tray_json_extract_double(const char *json, const char *key)
{
    if (json == NULL || key == NULL) {
        return -1.0;
    }
    char pattern[80];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1.0;
    }
    const char *colon = strchr(pos + strlen(pattern), ':');
    if (colon == NULL) {
        return -1.0;
    }
    return strtod(colon + 1, NULL);
}

static int wt_tray_json_extract_wstring(const char *json, const char *key,
                                        wchar_t *out, size_t out_count)
{
    if (json == NULL || key == NULL || out == NULL || out_count == 0) {
        return 0;
    }
    char pattern[80];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return 0;
    }
    const char *colon = strchr(pos + strlen(pattern), ':');
    if (colon == NULL) {
        return 0;
    }
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) {
        return 0;
    }
    q1++;
    const char *q2 = strchr(q1, '"');
    if (q2 == NULL) {
        return 0;
    }
    char buf[256];
    size_t n = (size_t)(q2 - q1);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, q1, n);
    buf[n] = '\0';
    return MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, (int)out_count) > 0;
}

static int wt_tray_read_file_utf8(const wchar_t *path, char **out_body,
                                  size_t *out_len)
{
    *out_body = NULL;
    *out_len = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return 0;
    }
    char *buf = (char *)malloc((size_t)size.QuadPart + 1u);
    if (buf == NULL) {
        CloseHandle(h);
        return 0;
    }
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)size.QuadPart, &read, NULL);
    CloseHandle(h);
    if (!ok) {
        free(buf);
        return 0;
    }
    buf[read] = '\0';
    *out_body = buf;
    *out_len = read;
    return 1;
}

static void wt_tray_status_fill_identity(WT_TrayStatusSummary *summary)
{
    WT_OsInfo os;
    if (wt_collect_os_info(&os) == WT_OK) {
        if (summary->hostname[0] == L'\0' && os.hostname[0] != L'\0') {
            StringCchCopyW(summary->hostname, ARRAYSIZE(summary->hostname),
                           os.hostname);
        }
        if (summary->os_line[0] == L'\0') {
            StringCchPrintfW(summary->os_line, ARRAYSIZE(summary->os_line),
                             L"%s %s", os.product_name,
                             os.arch[0] ? os.arch : L"");
        }
    }

    WT_PowerInfo power;
    if (wt_collect_power_info(&power) == WT_OK) {
        if (power.active_name[0] != L'\0') {
            StringCchCopyW(summary->power_plan, ARRAYSIZE(summary->power_plan),
                           power.active_name);
        }
        if (power.on_ac > 0) {
            StringCchCopyW(summary->power_source,
                           ARRAYSIZE(summary->power_source), L"AC");
        } else if (power.on_ac == 0) {
            if (power.battery_percent >= 0) {
                StringCchPrintfW(summary->power_source,
                                 ARRAYSIZE(summary->power_source),
                                 L"Battery %d%%", power.battery_percent);
            } else {
                StringCchCopyW(summary->power_source,
                               ARRAYSIZE(summary->power_source), L"Battery");
            }
        }
    }
}

static void wt_tray_status_fill_live(WT_TrayStatusSummary *summary)
{
    WT_MemoryMetrics mem;
    if (wt_collect_memory_metrics(&mem) == WT_OK) {
        summary->memory_percent = mem.used_percent;
        summary->live_sample = 1;
        summary->available = 1;
    }

    /* Short PDH window — keeps the tip/status useful without a full scan. */
    WT_CpuMetrics cpu;
    if (wt_collect_cpu_metrics(150, &cpu) == WT_OK && cpu.available) {
        summary->cpu_percent = cpu.total_usage_percent;
        summary->live_sample = 1;
        summary->available = 1;
    }
}

void wt_tray_status_refresh(WT_TrayStatusSummary *summary)
{
    if (summary == NULL) {
        return;
    }
    ZeroMemory(summary, sizeof(*summary));
    summary->cpu_percent = -1.0;
    summary->memory_percent = -1.0;
    summary->service_connected = wt_service_client_is_available(500);

    wchar_t path[MAX_PATH];
    WT_Result r = wt_paths_last_scan_file(path, ARRAYSIZE(path));
    if (r == WT_OK && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        char *body = NULL;
        size_t len = 0;
        if (wt_tray_read_file_utf8(path, &body, &len)) {
            summary->available = 1;
            StringCchCopyW(summary->source_path, ARRAYSIZE(summary->source_path),
                           path);
            StringCchCopyW(summary->source_label,
                           ARRAYSIZE(summary->source_label),
                           summary->service_connected
                               ? L"WinTune service cache + live sample"
                               : L"Cached scan + live sample");

            double cpu = wt_tray_json_extract_double(body, "total_usage_percent");
            if (cpu < 0.0) {
                cpu = wt_tray_json_extract_double(body, "cpu_total_percent");
            }
            if (cpu >= 0.0) {
                summary->cpu_percent = cpu;
            }

            double mem = wt_tray_json_extract_double(body, "used_percent");
            if (mem < 0.0) {
                mem = wt_tray_json_extract_double(body, "memory_used_percent");
            }
            if (mem >= 0.0) {
                summary->memory_percent = mem;
            }

            wchar_t host[64];
            if (wt_tray_json_extract_wstring(body, "hostname", host,
                                             ARRAYSIZE(host))) {
                StringCchCopyW(summary->hostname, ARRAYSIZE(summary->hostname),
                               host);
            }

            wchar_t os_name[96];
            if (wt_tray_json_extract_wstring(body, "os", os_name,
                                             ARRAYSIZE(os_name))) {
                wchar_t arch[32];
                if (wt_tray_json_extract_wstring(body, "arch", arch,
                                                 ARRAYSIZE(arch))) {
                    StringCchPrintfW(summary->os_line,
                                     ARRAYSIZE(summary->os_line), L"%s %s",
                                     os_name, arch);
                } else {
                    StringCchCopyW(summary->os_line, ARRAYSIZE(summary->os_line),
                                   os_name);
                }
            }

            wchar_t plan[64];
            if (wt_tray_json_extract_wstring(body, "plan_name", plan,
                                             ARRAYSIZE(plan))) {
                StringCchCopyW(summary->power_plan,
                               ARRAYSIZE(summary->power_plan), plan);
            }

            free(body);
        }
    }

    if (summary->source_label[0] == L'\0') {
        StringCchCopyW(summary->source_label, ARRAYSIZE(summary->source_label),
                       L"Live sample (no cached scan yet)");
    }

    wt_tray_status_fill_identity(summary);
    wt_tray_status_fill_live(summary);
}

void wt_tray_status_format_tip(const WT_TrayStatusSummary *s, wchar_t *out,
                               size_t out_count)
{
    if (out == NULL || out_count == 0) {
        return;
    }
    if (s == NULL) {
        StringCchCopyW(out, out_count, L"WinTune");
        return;
    }

    wchar_t cpu[32] = L"--";
    wchar_t mem[32] = L"--";
    if (s->cpu_percent >= 0.0) {
        StringCchPrintfW(cpu, ARRAYSIZE(cpu), L"%.0f%%", s->cpu_percent);
    }
    if (s->memory_percent >= 0.0) {
        StringCchPrintfW(mem, ARRAYSIZE(mem), L"%.0f%%", s->memory_percent);
    }

    if (s->power_plan[0] != L'\0') {
        StringCchPrintfW(out, out_count, L"WinTune  CPU %s  RAM %s  %s", cpu,
                         mem, s->power_plan);
    } else {
        StringCchPrintfW(out, out_count, L"WinTune  CPU %s  RAM %s", cpu, mem);
    }
}

static void wt_tray_status_format_text(const WT_TrayStatusSummary *s,
                                       wchar_t *out, size_t out_count)
{
    if (s == NULL || out == NULL || out_count == 0) {
        return;
    }

    wchar_t cpu_line[64] = L"CPU: (unavailable)";
    wchar_t mem_line[64] = L"Memory: (unavailable)";
    wchar_t power_line[96] = L"Power: (unavailable)";
    if (s->cpu_percent >= 0.0) {
        StringCchPrintfW(cpu_line, ARRAYSIZE(cpu_line), L"CPU: %.1f%%",
                         s->cpu_percent);
    }
    if (s->memory_percent >= 0.0) {
        StringCchPrintfW(mem_line, ARRAYSIZE(mem_line), L"Memory: %.1f%%",
                         s->memory_percent);
    }
    if (s->power_plan[0] != L'\0') {
        if (s->power_source[0] != L'\0') {
            StringCchPrintfW(power_line, ARRAYSIZE(power_line),
                             L"Power: %s (%s)", s->power_plan, s->power_source);
        } else {
            StringCchPrintfW(power_line, ARRAYSIZE(power_line), L"Power: %s",
                             s->power_plan);
        }
    }

    const wchar_t *svc = s->service_connected ? L"Connected"
                                              : L"Not running (CLI-only mode)";
    const wchar_t *live =
        s->live_sample ? L"Live sample included"
                       : L"Cache / identity only (live sample unavailable)";

    StringCchPrintfW(
        out, out_count,
        L"WinTune status (read-only)\r\n\r\n"
        L"Service: %s\r\n"
        L"%s\r\n"
        L"%s\r\n"
        L"%s\r\n"
        L"Host: %s\r\n"
        L"OS: %s\r\n\r\n"
        L"Source: %s\r\n"
        L"%s\r\n\r\n"
        L"Run Doctor from the tray menu for a full scan.\r\n"
        L"Mutating actions always require confirmation in the CLI.",
        svc, cpu_line, mem_line, power_line,
        s->hostname[0] ? s->hostname : L"(unknown)",
        s->os_line[0] ? s->os_line : L"(unknown)", s->source_label, live);
}

typedef struct WT_TrayStatusWnd {
    HWND text;
    WT_TrayStatusSummary summary;
} WT_TrayStatusWnd;

static void wt_tray_status_update_ui(WT_TrayStatusWnd *ctx)
{
    if (ctx == NULL || ctx->text == NULL) {
        return;
    }
    wt_tray_status_refresh(&ctx->summary);
    wchar_t buf[1400];
    wt_tray_status_format_text(&ctx->summary, buf, ARRAYSIZE(buf));
    SetWindowTextW(ctx->text, buf);
}

static LRESULT CALLBACK wt_tray_status_wnd_proc(HWND hwnd, UINT msg,
                                                WPARAM wparam, LPARAM lparam)
{
    WT_TrayStatusWnd *ctx = (WT_TrayStatusWnd *)GetWindowLongPtrW(
        hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        ctx = (WT_TrayStatusWnd *)calloc(1, sizeof(WT_TrayStatusWnd));
        if (ctx == NULL) {
            return -1;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);

        ctx->text = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                              ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            12, 12, 400, 260, hwnd, NULL, NULL, NULL);
        SendMessageW(ctx->text, WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

        CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE,
                        12, 284, 90, 28, hwnd, (HMENU)WT_ID_REFRESH, NULL,
                        NULL);
        CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
                        110, 284, 90, 28, hwnd, (HMENU)WT_ID_CLOSE, NULL,
                        NULL);
        wt_tray_status_update_ui(ctx);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == WT_ID_REFRESH && ctx != NULL) {
            wt_tray_status_update_ui(ctx);
            return 0;
        }
        if (LOWORD(wparam) == WT_ID_CLOSE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (ctx != NULL) {
            free(ctx);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int wt_tray_status_register_class(void)
{
    static int registered = 0;
    if (registered) {
        return 1;
    }
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wt_tray_status_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WT_TRAY_STATUS_CLASS;
    wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_WINTUNE));
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 0;
    }
    registered = 1;
    return 1;
}

HWND wt_tray_status_show(HWND owner)
{
    if (!wt_tray_status_register_class()) {
        return NULL;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, WT_TRAY_STATUS_CLASS, L"WinTune Status",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
        440, 370, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (hwnd == NULL) {
        return NULL;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}
