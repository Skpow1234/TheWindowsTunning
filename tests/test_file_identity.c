#include "system/file_identity.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_wcs(const wchar_t *got, const wchar_t *want, const char *label)
{
    if (got == NULL || want == NULL || wcscmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s: got '%ls' want '%ls'\n", label,
                got ? got : L"(null)", want ? want : L"(null)");
        g_failed = 1;
    }
}

static void expect_ok(WT_Result r, const char *label)
{
    if (r != WT_OK) {
        fprintf(stderr, "FAIL %s: result %d\n", label, (int)r);
        g_failed = 1;
    }
}

int main(void)
{
    wchar_t path[MAX_PATH];

    expect_ok(wt_identity_extract_path(L"C:\\Windows\\System32\\notepad.exe",
                                       path, MAX_PATH),
              "plain path");
    expect_wcs(path, L"C:\\Windows\\System32\\notepad.exe", "plain path value");

    expect_ok(wt_identity_extract_path(
                  L"\"C:\\Program Files\\App\\app.exe\" --flag", path, MAX_PATH),
              "quoted path");
    expect_wcs(path, L"C:\\Program Files\\App\\app.exe", "quoted path value");

    expect_ok(wt_identity_extract_path(L"C:\\Tools\\tool.exe /silent", path,
                                       MAX_PATH),
              "unquoted with args");
    expect_wcs(path, L"C:\\Tools\\tool.exe", "unquoted with args value");

    if (wt_identity_extract_path(L"   ", path, MAX_PATH) == WT_OK) {
        fputs("FAIL empty should not succeed\n", stderr);
        g_failed = 1;
    }

    if (strcmp(wt_signature_status_name(WT_SIG_UNSIGNED), "unsigned") != 0 ||
        strcmp(wt_publisher_origin_name(WT_ORIGIN_MICROSOFT), "microsoft") != 0) {
        fputs("FAIL status/origin name strings\n", stderr);
        g_failed = 1;
    }

    return g_failed ? 1 : 0;
}
