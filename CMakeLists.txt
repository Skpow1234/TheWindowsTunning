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

    # platform / service IPC
    src/platform/service_ipc.c
    src/platform/service_client.c

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
    src/system/tasks.c
    src/system/updates.c
    src/system/blockers.c
    src/system/services.c
    src/system/boot.c
    src/system/boot_etw.c
    src/system/privilege.c

    # service (Phase 11)
    src/service/service_install.c
    src/service/service_main.c
    src/service/service_dispatch.c

    # actions
    src/actions/rollback.c
    src/actions/safe_actions.c
    src/actions/action_map.c
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
    src/output/json_error.c

    # tui
    src/tui/tui.c
    src/tui/tui_screen.c
    src/tui/tui_theme.c
    src/tui/tui_widgets.c
    src/tui/tui_input.c

    # cli
    src/cli/cli.c
    src/cli/cli_exit.c
    src/cli/cli_launcher.c
    src/cli/exit_codes.c
    src/cli/commands_scan.c
    src/cli/commands_top.c
    src/cli/commands_recommend.c
    src/cli/commands_doctor.c
    src/cli/commands_startup.c
    src/cli/commands_tasks.c
    src/cli/commands_updates.c
    src/cli/commands_blockers.c
    src/cli/commands_services.c
    src/cli/commands_power.c
    src/cli/commands_apply.c
    src/cli/commands_rollback.c
    src/cli/commands_report.c
    src/cli/commands_boot.c
    src/cli/commands_service.c

    # tray (Phase 19)
    src/tray/tray.c
    src/tray/tray_status.c
    src/cli/commands_tray.c
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
install(FILES
    pack/QUICKSTART.txt
    pack/Run-Doctor.cmd
    pack/Run-Help.cmd
    pack/Launch-WinTune.cmd
    pack/Launch-WinTune.ps1
    pack/Start-Tray.cmd
    DESTINATION .
    OPTIONAL
)

# Windows libraries linked as modules require them:
#   pdh      -> PDH performance counters (cpu, disk activity)
#   psapi    -> process memory/IO counters (process)
#   advapi32 -> registry (os_info), services, ETW trace sessions
#   wevtapi  -> boot performance event log (boot analyze)
#   powrprof -> power scheme detection (power)
#   iphlpapi -> network interface counters (network/tui)
target_link_libraries(wintune PRIVATE
    pdh
    psapi
    advapi32
    wevtapi
    powrprof
    iphlpapi
    taskschd
    ole32
    oleaut32
    wuguid
    rstrtmgr
    shell32
)

enable_testing()

add_executable(wintune_unit_tests
    tests/test_exit_codes.c
    src/cli/exit_codes.c
    src/common/error.c
)
target_include_directories(wintune_unit_tests PRIVATE src)
if(MSVC)
    target_compile_options(wintune_unit_tests PRIVATE /W4 /permissive-)
    if(WINTUNE_WARNINGS_AS_ERRORS)
        target_compile_options(wintune_unit_tests PRIVATE /WX)
    endif()
endif()
add_test(NAME exit_codes COMMAND wintune_unit_tests)
