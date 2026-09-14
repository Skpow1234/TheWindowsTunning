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

static void expect_loc(WT_InstallLocation got, WT_InstallLocation want,
                       const char *label)
{
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %s want %s\n", label,
                wt_install_location_name(got), wt_install_location_name(want));
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
        strcmp(wt_publisher_origin_name(WT_ORIGIN_MICROSOFT), "microsoft") != 0 ||
        strcmp(wt_install_location_name(WT_LOC_TEMP), "temp") != 0) {
        fputs("FAIL status/origin/location name strings\n", stderr);
        g_failed = 1;
    }

    expect_loc(wt_identity_classify_location(
                   L"C:\\Windows\\System32\\svchost.exe"),
               WT_LOC_WINDOWS, "system32");
    expect_loc(wt_identity_classify_location(
                   L"C:\\Program Files\\App\\app.exe"),
               WT_LOC_PROGRAM_FILES, "program files");
    expect_loc(wt_identity_classify_location(
                   L"C:\\Program Files (x86)\\App\\app.exe"),
               WT_LOC_PROGRAM_FILES_X86, "program files x86");
    expect_loc(wt_identity_classify_location(
                   L"C:\\Users\\test\\AppData\\Local\\App\\app.exe"),
               WT_LOC_USER_APP_DATA, "appdata local");
    expect_loc(wt_identity_classify_location(
                   L"C:\\Users\\test\\AppData\\Local\\Temp\\setup.exe"),
               WT_LOC_TEMP, "temp under local");
    expect_loc(wt_identity_classify_location(
                   L"C:\\Users\\test\\Downloads\\tool.exe"),
               WT_LOC_DOWNLOADS, "downloads");
    expect_loc(wt_identity_classify_location(L"D:\\Steam\\steam.exe"),
               WT_LOC_OTHER, "other drive");

    /* Live provenance smoke: notepad should resolve product/company when present. */
    WT_FileIdentity id;
    if (wt_identity_from_path(L"C:\\Windows\\System32\\notepad.exe", &id) ==
        WT_OK) {
        if (!id.available || id.location != WT_LOC_WINDOWS) {
            fputs("FAIL notepad identity location/available\n", stderr);
            g_failed = 1;
        }
        if (id.unusual_location) {
            fputs("FAIL notepad should not be unusual_location\n", stderr);
            g_failed = 1;
        }
    }

    return g_failed ? 1 : 0;
}
