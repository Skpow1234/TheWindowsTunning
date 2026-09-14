#include "core/impact_score.h"

#include <stdio.h>
#include <string.h>

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
    WT_ImpactInput in;
    WT_ImpactScore out;

    memset(&in, 0, sizeof(in));
    in.cpu_percent = -1.0;
    in.disk_bytes_per_sec = -1.0;
    in.name = L"helper";
    in.command = L"C:\\Tools\\helper.exe";
    wt_impact_score_compute(&in, &out);
    expect_true(out.band == WT_STARTUP_IMPACT_UNKNOWN || out.score == 0,
                "empty evidence stays unknown/low");

    in.name = L"Steam";
    in.command = L"D:\\Steam\\steam.exe -silent";
    in.origin = WT_ORIGIN_THIRD_PARTY;
    wt_impact_score_compute(&in, &out);
    expect_true(out.evidence & WT_IMPACT_EV_HEURISTIC, "steam heuristic");
    expect_true(out.evidence & WT_IMPACT_EV_PUBLISHER, "third-party publisher");
    expect_true(out.band == WT_STARTUP_IMPACT_MEDIUM ||
                    out.band == WT_STARTUP_IMPACT_LOW,
                "steam without measured is med/low");
    expect_true(out.score >= 20 && out.score < WT_IMPACT_RECOMMEND_SCORE_MIN,
                "steam heuristic alone below recommend threshold");

    in.measured_available = 1;
    in.measured_ms = 12000;
    wt_impact_score_compute(&in, &out);
    expect_true(out.score >= WT_IMPACT_RECOMMEND_SCORE_MIN, "measured 12s high");
    expect_true(out.band == WT_STARTUP_IMPACT_HIGH, "band high");
    expect_true(out.confidence_percent >= WT_IMPACT_RECOMMEND_CONF_MIN,
                "confidence with measured");

    /* Microsoft origin pulls score down. */
    in.origin = WT_ORIGIN_MICROSOFT;
    in.microsoft_protected = 1;
    in.name = L"SecurityHealth";
    in.command = L"C:\\Windows\\System32\\SecurityHealthSystray.exe";
    in.measured_available = 1;
    in.measured_ms = 4000;
    wt_impact_score_compute(&in, &out);
    expect_true(out.evidence & WT_IMPACT_EV_PUBLISHER, "ms publisher bit");
    expect_true(out.score < 60, "microsoft dampens measured medium");

    /* Runtime sample can push a third-party app over the line. */
    memset(&in, 0, sizeof(in));
    in.cpu_percent = 20.0;
    in.working_set_bytes = 600ull * 1024ull * 1024ull;
    in.disk_bytes_per_sec = 6.0 * 1024.0 * 1024.0;
    in.name = L"Discord";
    in.command = L"C:\\Users\\x\\AppData\\Local\\Discord\\Discord.exe";
    in.origin = WT_ORIGIN_THIRD_PARTY;
    wt_impact_score_compute(&in, &out);
    expect_true(out.evidence & WT_IMPACT_EV_RUNTIME, "runtime evidence");
    expect_true(out.score >= WT_IMPACT_RECOMMEND_SCORE_MIN,
                "runtime+heuristic reaches recommend");

    expect_true(wt_impact_score_to_band(70, WT_IMPACT_EV_MEASURED) ==
                    WT_STARTUP_IMPACT_HIGH,
                "band map high");
    expect_true(wt_impact_score_to_band(40, WT_IMPACT_EV_HEURISTIC) ==
                    WT_STARTUP_IMPACT_MEDIUM,
                "band map medium");

    return g_failed ? 1 : 0;
}
