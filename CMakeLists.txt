cmake_minimum_required(VERSION 3.24)
project(wintune C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(WINTUNE_VERSION "0.1.0" CACHE STRING "WinTune version string (release CI may override)")
option(WINTUNE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI lint)" OFF)

# Source list grows per phase as modules are implemented.
add_executable(wintune
    src/main.c

    # common
    src/common/error.c
    src/common/log.c
    src/common/units.c

    # platform
    src/platform/console.c
    src/platform/time.c
    src/platform/paths.c

    # metrics
    src/metrics/pdh_utils.c
    src/metrics/cpu.c
    src/metrics/memory.c
    src/metrics/disk.c
    src/metrics/process.c
    src/metrics/network.c

    # system
    src/system/os_info.c
    src/system/power.c
    src/system/startup.c
    src/system/services.c
    src/system/privilege.c

    # actions
    src/actions/rollback.c
    src/actions/safe_actions.c
    src/actions/apply.c

    # core
    src/core/report_model.c
    src/core/scan.c
    src/core/recommendations.c

    # output
    src/output/table.c
    src/output/text.c
    src/output/text_report.c
    src/output/json.c

    # tui
    src/tui/tui.c
    src/tui/tui_screen.c
    src/tui/tui_theme.c
    src/tui/tui_widgets.c
    src/tui/tui_input.c

    # cli
    src/cli/cli.c
    src/cli/commands_scan.c
    src/cli/commands_top.c
    src/cli/commands_recommend.c
    src/cli/commands_doctor.c
    src/cli/commands_startup.c
    src/cli/commands_services.c
    src/cli/commands_power.c
    src/cli/commands_apply.c
    src/cli/commands_rollback.c
    src/cli/commands_report.c
)

target_include_directories(wintune PRIVATE src)

target_compile_definitions(wintune PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    WT_VERSION_STRING="${WINTUNE_VERSION}"
    # Target Windows 10+ so version-gated APIs (e.g. GetIfTable2) are visible.
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

if(MSVC)
    target_compile_options(wintune PRIVATE /W4 /permissive-)
    if(WINTUNE_WARNINGS_AS_ERRORS)
        target_compile_options(wintune PRIVATE /WX)
    endif()
endif()

install(TARGETS wintune RUNTIME DESTINATION .)
install(FILES README.md LICENSE DESTINATION . OPTIONAL)

# Windows libraries linked as modules require them:
#   pdh      -> PDH performance counters (cpu, disk activity)
#   psapi    -> process memory/IO counters (process)
#   advapi32 -> registry (os_info), services
#   powrprof -> power scheme detection (power)
#   iphlpapi -> network interface counters (network/tui)
target_link_libraries(wintune PRIVATE
    pdh
    psapi
    advapi32
    powrprof
    iphlpapi
)
