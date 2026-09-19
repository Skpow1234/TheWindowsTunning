#include "tui/tui_theme.h"

#include <stdio.h>
#include <string.h>

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
    WT_TuiTheme t;

    wt_tui_theme_init(&t, 1, 1);
    expect_true(t.preset == WT_TUI_THEME_DEFAULT, "default preset");
    expect_true(t.color == 1, "default color on");
    expect_true(!t.skip_sparklines, "default has sparks");

    wt_tui_theme_apply_preset(&t, L"compact");
    expect_true(t.preset == WT_TUI_THEME_COMPACT, "compact preset");
    expect_true(t.skip_sparklines, "compact skips sparks");
    expect_true(t.gauge_width == 16, "compact gauge width");

    wt_tui_theme_init(&t, 1, 1);
    wt_tui_theme_apply_preset(&t, L"mono");
    expect_true(t.preset == WT_TUI_THEME_MONO, "mono preset");
    expect_true(t.color == 0, "mono disables color");

    wt_tui_theme_init(&t, 0, 1);
    wt_tui_theme_apply_preset(&t, L"high-contrast");
    expect_true(t.preset == WT_TUI_THEME_HIGH_CONTRAST, "hc preset");
    expect_true(t.color == 1, "hc enables color");
    expect_true(t.a11y_labels, "hc a11y labels");
    expect_true(t.skip_sparklines, "hc skips sparks");
    expect_true(t.unicode == 0, "hc uses ascii glyphs");
    expect_true(strcmp(wt_tui_dim(&t), "\x1b[1m") == 0, "hc dim is bold");
    expect_true(strstr(wt_tui_color_for_pct(&t, 90.0), "41") != NULL,
                "hc high pct red bg");

    wt_tui_theme_init(&t, 1, 1);
    wt_tui_theme_apply_preset(&t, L"hc");
    expect_true(t.preset == WT_TUI_THEME_HIGH_CONTRAST, "hc alias");

    wt_tui_theme_init(&t, 1, 1);
    wt_tui_theme_apply_preset(&t, L"ssh");
    expect_true(t.preset == WT_TUI_THEME_SSH, "ssh preset");
    expect_true(t.safe_layout, "ssh safe layout");
    expect_true(t.a11y_labels, "ssh a11y");
    expect_true(t.color == 0, "ssh no color");
    expect_true(t.unicode == 0, "ssh ascii");

    wt_tui_theme_init(&t, 1, 1);
    wt_tui_theme_apply_preset(&t, L"high-contrast");
    wt_tui_theme_apply_safe_layout(&t);
    expect_true(t.safe_layout, "safe layout set");
    expect_true(t.color == 1, "safe keeps hc color");
    expect_true(t.a11y_labels, "safe keeps a11y");
    expect_true(t.skip_sparklines, "safe skips sparks");
    expect_true(t.unicode == 0, "safe forces ascii");

    wt_tui_theme_init(&t, 1, 1);
    wt_tui_theme_apply_safe_layout(&t);
    expect_true(t.color == 0, "safe clears default color");
    expect_true(t.preset == WT_TUI_THEME_SSH, "safe upgrades default to ssh");

    if (g_failed) {
        fputs("tui_theme tests failed\n", stderr);
        return 1;
    }
    fputs("tui_theme tests passed\n", stdout);
    return 0;
}
