#include "common/units.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_ok(WT_Result r, const char *label)
{
    if (r != WT_OK) {
        fprintf(stderr, "FAIL %s: result %d\n", label, (int)r);
        g_failed = 1;
    }
}

static void expect_wstr(const wchar_t *got, const wchar_t *want, const char *label)
{
    if (got == NULL || want == NULL || wcscmp(got, want) != 0) {
        fwprintf(stderr, L"FAIL %hs: got '%ls' want '%ls'\n", label,
                 got ? got : L"(null)", want ? want : L"(null)");
        g_failed = 1;
    }
}

int main(void)
{
    wchar_t buf[64];

    expect_ok(wt_format_bytes(0, buf, 64), "bytes0");
    expect_wstr(buf, L"0 B", "bytes0");

    expect_ok(wt_format_bytes(512, buf, 64), "bytes512");
    expect_wstr(buf, L"512 B", "bytes512");

    expect_ok(wt_format_bytes(1024ULL, buf, 64), "bytes1k");
    expect_wstr(buf, L"1.0 KB", "bytes1k");

    expect_ok(wt_format_bytes(1536ULL * 1024ULL * 1024ULL, buf, 64),
              "bytes1_5g");
    expect_wstr(buf, L"1.5 GB", "bytes1_5g");

    expect_ok(wt_format_duration_ms(0, buf, 64), "dur0");
    expect_wstr(buf, L"0m", "dur0");

    expect_ok(wt_format_duration_ms(90ULL * 1000ULL, buf, 64),
              "dur90s");
    expect_wstr(buf, L"1m", "dur90s");

    expect_ok(wt_format_duration_ms(3ULL * 3600ULL * 1000ULL + 5ULL * 60ULL * 1000ULL,
                                    buf, 64),
              "dur3h5m");
    expect_wstr(buf, L"3h 05m", "dur3h5m");

    expect_ok(wt_format_duration_ms(2ULL * 86400ULL * 1000ULL + 4ULL * 3600ULL * 1000ULL,
                                    buf, 64),
              "dur2d4h");
    expect_wstr(buf, L"2d 04h", "dur2d4h");

    if (wt_format_bytes(1, NULL, 0) != WT_ERR_INVALID_ARGUMENT) {
        fputs("FAIL bytes null out\n", stderr);
        g_failed = 1;
    }

    if (g_failed) {
        fputs("units tests failed\n", stderr);
        return 1;
    }
    fputs("units tests passed\n", stdout);
    return 0;
}
