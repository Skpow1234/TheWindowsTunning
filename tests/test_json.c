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

int main(void)
{
    char *buf = NULL;
    size_t size = 0;
    FILE *mem = NULL;
#if defined(_MSC_VER)
    if (tmpfile_s(&mem) != 0) {
        mem = NULL;
    }
#else
    mem = tmpfile();
#endif
    if (mem == NULL) {
        fputs("FAIL could not open tmpfile\n", stderr);
        return 1;
    }

    WT_JsonWriter w;
    wt_json_init(&w, mem);
    wt_json_set_compact(&w, 1);
    wt_json_begin_object(&w);
    wt_json_key(&w, "schema_version");
    wt_json_string(&w, WT_JSON_SCHEMA_VERSION);
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
    fflush(mem);

    if (fseek(mem, 0, SEEK_END) != 0) {
        fputs("FAIL fseek end\n", stderr);
        fclose(mem);
        return 1;
    }
    long len = ftell(mem);
    if (len < 0) {
        fputs("FAIL ftell\n", stderr);
        fclose(mem);
        return 1;
    }
    if (fseek(mem, 0, SEEK_SET) != 0) {
        fputs("FAIL fseek set\n", stderr);
        fclose(mem);
        return 1;
    }
    buf = (char *)malloc((size_t)len + 1u);
    if (buf == NULL) {
        fclose(mem);
        return 1;
    }
    size = fread(buf, 1, (size_t)len, mem);
    buf[size] = '\0';
    fclose(mem);

    expect_contains(buf, "\"schema_version\":\"1.0.0\"", "schema");
    expect_contains(buf, "\"msg\":\"a\\\"b\\\\c\"", "escape");
    expect_contains(buf, "\"n\":null", "null");
    expect_contains(buf, "\"ok\":true", "bool");
    expect_contains(buf, "\"arr\":[1,2]", "array");
    /* finish() always appends a trailing newline even in compact mode. */
    expect_contains(buf, "}", "object closed");

    free(buf);

    if (g_failed) {
        fputs("json tests failed\n", stderr);
        return 1;
    }
    fputs("json tests passed\n", stdout);
    return 0;
}
