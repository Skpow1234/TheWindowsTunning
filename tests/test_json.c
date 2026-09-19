#include "output/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void expect_contains(const char *hay, const char *needle, const char *label)
{
    if (hay == NULL || needle == NULL || strstr(hay, needle) == NULL) {
        fprintf(stderr, "FAIL %s: missing '%s' in:\n%s\n", label,
                needle ? needle : "(null)", hay ? hay : "(null)");
        g_failed = 1;
    }
}

static void expect_not_contains(const char *hay, const char *needle,
                                const char *label)
{
    if (hay != NULL && needle != NULL && strstr(hay, needle) != NULL) {
        fprintf(stderr, "FAIL %s: unexpected '%s' in:\n%s\n", label, needle,
                hay);
        g_failed = 1;
    }
}

static FILE *open_tmp(void)
{
    FILE *mem = NULL;
#if defined(_MSC_VER)
    if (tmpfile_s(&mem) != 0) {
        return NULL;
    }
#else
    mem = tmpfile();
#endif
    return mem;
}

static char *read_tmp(FILE *mem)
{
    char *buf;
    long len;
    size_t size;

    fflush(mem);
    if (fseek(mem, 0, SEEK_END) != 0) {
        return NULL;
    }
    len = ftell(mem);
    if (len < 0) {
        return NULL;
    }
    if (fseek(mem, 0, SEEK_SET) != 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)len + 1u);
    if (buf == NULL) {
        return NULL;
    }
    size = fread(buf, 1, (size_t)len, mem);
    buf[size] = '\0';
    return buf;
}

static int run_emit(int major, const char **out_buf)
{
    FILE *mem = open_tmp();
    WT_JsonWriter w;
    char *buf;

    if (mem == NULL) {
        fputs("FAIL could not open tmpfile\n", stderr);
        return 1;
    }

    wt_json_set_emit_major(major);
    wt_json_init(&w, mem);
    wt_json_set_compact(&w, 1);
    wt_json_begin_object(&w);
    wt_json_emit_schema_meta(&w, "scan");
    wt_json_key(&w, "msg");
    wt_json_string(&w, "a\"b\\c");
    wt_json_key(&w, "n");
    wt_json_null(&w);
    wt_json_key(&w, "ok");
    wt_json_bool(&w, 1);
    wt_json_key(&w, "arr");
    wt_json_begin_array(&w);
    wt_json_uint64(&w, 1);
    wt_json_uint64(&w, 2);
    wt_json_end_array(&w);
    wt_json_end_object(&w);
    wt_json_finish(&w);

    buf = read_tmp(mem);
    fclose(mem);
    if (buf == NULL) {
        fputs("FAIL read tmp\n", stderr);
        return 1;
    }
    *out_buf = buf;
    return 0;
}

int main(void)
{
    const char *buf = NULL;

    if (run_emit(2, &buf) != 0) {
        return 1;
    }
    expect_contains(buf, "\"schema_version\":\"2.0.0\"", "schema v2");
    expect_contains(buf, "\"schema_compat_min\":\"1.0.0\"", "compat");
    expect_contains(buf, "\"document\":\"scan\"", "document");
    expect_contains(buf, "\"msg\":\"a\\\"b\\\\c\"", "escape");
    expect_contains(buf, "\"n\":null", "null");
    expect_contains(buf, "\"ok\":true", "bool");
    expect_contains(buf, "\"arr\":[1,2]", "array");
    free((void *)buf);

    if (run_emit(1, &buf) != 0) {
        return 1;
    }
    expect_contains(buf, "\"schema_version\":\"1.0.0\"", "schema v1 pin");
    expect_not_contains(buf, "schema_compat_min", "no compat on v1");
    expect_not_contains(buf, "\"document\"", "no document on v1");
    free((void *)buf);

    if (g_failed) {
        fputs("json tests failed\n", stderr);
        return 1;
    }
    fputs("json tests passed\n", stdout);
    return 0;
}
