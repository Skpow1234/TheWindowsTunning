#include "system/uninstall.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        g_failed = 1;
    }
}

int main(void)
{
    expect_true(wt_collect_uninstall_advice(NULL, 0, 0) == WT_ERR_INVALID_ARGUMENT,
                "null out");

    WT_UninstallAdvice *advice =
        (WT_UninstallAdvice *)malloc(sizeof(WT_UninstallAdvice));
    expect_true(advice != NULL, "alloc");
    if (advice == NULL) {
        return 1;
    }

    WT_Result r = wt_collect_uninstall_advice(advice, 0, 0);
    expect_true(r == WT_OK, "collect without startup");
    expect_true(advice->scanned_keys > 0, "scanned some keys");
    expect_true(advice->count > 0, "found some apps");

    for (size_t i = 0; i < advice->count; ++i) {
        expect_true(advice->apps[i].display_name[0] != L'\0', "name set");
        expect_true(advice->apps[i].system_component == 0, "no system component");
    }

    free(advice);

    if (g_failed) {
        fputs("uninstall advice tests failed\n", stderr);
        return 1;
    }
    fputs("uninstall advice tests passed\n", stdout);
    return 0;
}
