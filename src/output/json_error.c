#include "output/json_error.h"

#include "cli/cli.h"
#include "cli/exit_codes.h"
#include "output/json.h"
#include "platform/console.h"
#include "platform/time.h"

#include <stdio.h>

void wt_json_emit_error(FILE *out,
                        const struct WT_CliOptions *opts,
                        const wchar_t *command,
                        WT_Result result,
                        const char *message,
                        int exit_code)
{
    (void)opts;
    if (out == NULL) {
        return;
    }

    WT_JsonWriter w;
    wt_json_init(&w, out);
    wt_json_set_compact(&w, 1);

    char ts[32];
    if (wt_now_iso8601_utc(ts, sizeof(ts)) != WT_OK) {
        ts[0] = '\0';
    }

    wt_json_begin_object(&w);
    wt_json_key(&w, "schema_version");
    wt_json_string(&w, WT_JSON_SCHEMA_VERSION);
    wt_json_key(&w, "version");
    wt_json_string(&w, WT_VERSION_STRING);
    wt_json_key(&w, "timestamp_utc");
    wt_json_string(&w, ts);
    wt_json_key(&w, "ok");
    wt_json_bool(&w, 0);

    wt_json_key(&w, "session");
    wt_json_begin_object(&w);
    wt_json_key(&w, "interactive");
    wt_json_bool(&w, wt_session_is_interactive());
    wt_json_key(&w, "remote");
    wt_json_bool(&w, wt_session_is_remote());
    wt_json_end_object(&w);

    wt_json_key(&w, "error");
    wt_json_begin_object(&w);
    wt_json_key(&w, "code");
    wt_json_string(&w, wt_result_code_name(result));
    wt_json_key(&w, "exit_code");
    wt_json_uint64(&w, (unsigned long long)exit_code);
    wt_json_key(&w, "exit_name");
    wt_json_string(&w, wt_exit_code_name(exit_code));
    if (command != NULL && command[0] != L'\0') {
        wt_json_key(&w, "command");
        wt_json_wstring(&w, command);
    }
    if (message != NULL && message[0] != '\0') {
        wt_json_key(&w, "message");
        wt_json_string(&w, message);
    }
    wt_json_end_object(&w);

    wt_json_end_object(&w);
    wt_json_finish(&w);
}
