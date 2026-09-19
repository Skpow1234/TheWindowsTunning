#include "platform/service_ipc.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int g_failed = 0;

static void expect_true(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failed = 1;
    }
}

int main(void)
{
    WT_IpcAclMode m = WT_IPC_ACL_ADMIN;

    expect_true(wt_service_ipc_acl_from_name(L"admin", &m) == WT_OK, "admin");
    expect_true(m == WT_IPC_ACL_ADMIN, "admin mode");
    expect_true(strcmp(wt_service_ipc_acl_name(m), "admin") == 0, "admin name");

    expect_true(wt_service_ipc_acl_from_name(L"default", &m) == WT_OK,
                "default");
    expect_true(m == WT_IPC_ACL_ADMIN, "default=admin");

    expect_true(wt_service_ipc_acl_from_name(L"admin-only", &m) == WT_OK,
                "admin-only");
    expect_true(m == WT_IPC_ACL_ADMIN_ONLY, "admin-only mode");
    expect_true(strcmp(wt_service_ipc_acl_name(m), "admin-only") == 0,
                "admin-only name");

    expect_true(wt_service_ipc_acl_from_name(L"strict", &m) == WT_OK, "strict");
    expect_true(m == WT_IPC_ACL_ADMIN_ONLY, "strict=admin-only");

    expect_true(wt_service_ipc_acl_from_name(L"nope", &m) == WT_ERR_NOT_FOUND,
                "unknown");

    const wchar_t *sddl_admin = wt_service_ipc_acl_sddl(WT_IPC_ACL_ADMIN);
    const wchar_t *sddl_strict = wt_service_ipc_acl_sddl(WT_IPC_ACL_ADMIN_ONLY);
    expect_true(sddl_admin != NULL && wcsstr(sddl_admin, L"BA") != NULL,
                "admin sddl has BA");
    expect_true(sddl_strict != NULL && wcsstr(sddl_strict, L"WD") != NULL,
                "admin-only sddl denies WD");
    expect_true(wcsstr(sddl_strict, L":P(") != NULL ||
                    wcsstr(sddl_strict, L"D:P") != NULL,
                "admin-only protected DACL");

    expect_true(wt_service_ipc_acl_describe(WT_IPC_ACL_ADMIN) != NULL,
                "describe admin");
    expect_true(wt_service_ipc_acl_describe(WT_IPC_ACL_ADMIN_ONLY) != NULL,
                "describe admin-only");

    if (g_failed) {
        fputs("service_ipc_acl tests failed\n", stderr);
        return 1;
    }
    fputs("service_ipc_acl tests passed\n", stdout);
    return 0;
}
