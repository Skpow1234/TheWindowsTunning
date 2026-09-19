cmake_minimum_required(VERSION 3.24)
project(wintune C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(WINTUNE_VERSION "0.2.0" CACHE STRING "WinTune version string (release CI may override)")
option(WINTUNE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI lint)" OFF)

# Parse semver for VERSIONINFO (strip pre-release suffix like -beta).
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1" WINTUNE_VERSION_MAJOR "${WINTUNE_VERSION}")
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\2" WINTUNE_VERSION_MINOR "${WINTUNE_VERSION}")
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\3" WINTUNE_VERSION_PATCH "${WINTUNE_VERSION}")
if(WINTUNE_VERSION_MAJOR STREQUAL "" OR WINTUNE_VERSION_MAJOR STREQUAL "${WINTUNE_VERSION}")
    set(WINTUNE_VERSION_MAJOR 0)
    set(WINTUNE_VERSION_MINOR 1)
    set(WINTUNE_VERSION_PATCH 0)
endif()
set(WINTUNE_VERSION_FULL "${WINTUNE_VERSION_MAJOR}.${WINTUNE_VERSION_MINOR}.${WINTUNE_VERSION_PATCH}.0")

configure_file(
    ${CMAKE_SOURCE_DIR}/resources/wintune.rc.in
    ${CMAKE_BINARY_DIR}/wintune.rc
    @ONLY
)
# Copy icon/manifest/header next to generated .rc (RC paths are relative to the .rc file).
configure_file(${CMAKE_SOURCE_DIR}/resources/wintune.ico
               ${CMAKE_BINARY_DIR}/wintune.ico COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/resources/wintune.manifest
               ${CMAKE_BINARY_DIR}/wintune.manifest COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/resources/resource.h
               ${CMAKE_BINARY_DIR}/resource.h COPYONLY)

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
    src/metrics/gpu.c

    # system
    src/system/os_info.c
    src/system/power.c
    src/system/startup.c
    src/system/tasks.c
    src/system/updates.c
    src/system/blockers.c
    src/system/services.c
    src/system/storage_health.c
    src/system/reliability.c
    src/system/file_identity.c
    src/system/boot.c
    src/system/boot_etw.c
    src/system/privilege.c
    src/system/uninstall.c

    # service (Phase 11)
    src/service/service_install.c
    src/service/service_main.c
    src/service/service_dispatch.c
    src/service/service_policy.c

    # actions
    src/actions/rollback.c
    src/actions/safe_actions.c
    src/actions/delay_plan.c
    src/actions/action_map.c
    src/actions/apply.c
    src/actions/apply_preview.c

    # core
    src/core/report_model.c
    src/core/scan.c
    src/core/recommendations.c
    src/core/confidence.c
    src/core/impact_score.c
    src/core/doctor_plan.c

    # fleet (Phase 46)
    src/fleet/fleet_pack.c
    src/common/sha256.c
    src/common/zip_store.c

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
    src/tui/tui_compare.c

    # cli
    src/cli/cli.c
    src/cli/cli_parse.c
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
    src/cli/commands_storage.c
    src/cli/commands_reliability.c
    src/cli/commands_apps.c
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
    src/cli/commands_fleet.c

    # Phase 21 resources (icon, VERSIONINFO, manifest)
    ${CMAKE_BINARY_DIR}/wintune.rc
)

target_include_directories(wintune PRIVATE src ${CMAKE_BINARY_DIR})

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
    # Prefer our embedded manifest over the default linker-generated one.
    target_link_options(wintune PRIVATE "/MANIFEST:NO")
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
    pack/Add-To-Path.cmd
    pack/Add-To-Path.ps1
    pack/Check-Arch.ps1
    DESTINATION .
    OPTIONAL
)

# Windows libraries linked as modules require them:
#   pdh      -> PDH performance counters (cpu, disk activity)
#   psapi    -> process memory/IO counters (process)
#   advapi32 -> registry (os_info), services, ETW trace sessions
#   wevtapi  -> boot performance event log (boot analyze)
#   powrprof -> power scheme detection (power)
#   iphlpapi -> network interface counters + TCP EStats (network/tui/top)
#   ws2_32   -> address family constants for TCP table sampling
#   dxgi     -> GPU adapter enumeration (Phase 28)
target_link_libraries(wintune PRIVATE
    pdh
    psapi
    advapi32
    wevtapi
    powrprof
    iphlpapi
    ws2_32
    dxgi
    dxguid
    taskschd
    ole32
    oleaut32
    wuguid
    rstrtmgr
    shell32
    wintrust
    version
    crypt32
    bcrypt
    rpcrt4
)

enable_testing()

function(wt_add_unit_test name)
    # Keep unit tests out of the default ALL_BUILD so Release packaging
    # (cmake --build without --target) is not blocked by test-only /WX noise.
    add_executable(${name} EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${name} PRIVATE src ${CMAKE_BINARY_DIR})
    target_compile_definitions(${name} PRIVATE
        WIN32_LEAN_AND_MEAN UNICODE _UNICODE
        WT_VERSION_STRING="${WINTUNE_VERSION}"
        _WIN32_WINNT=0x0A00 WINVER=0x0A00
    )
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive-)
        if(WINTUNE_WARNINGS_AS_ERRORS)
            target_compile_options(${name} PRIVATE /WX)
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

wt_add_unit_test(wintune_unit_tests
    tests/test_exit_codes.c
    src/cli/exit_codes.c
    src/common/error.c
)

wt_add_unit_test(test_units
    tests/test_units.c
    src/common/units.c
    src/common/error.c
)

wt_add_unit_test(test_cli_parser
    tests/test_cli_parser.c
    src/cli/cli_parse.c
    src/cli/exit_codes.c
    src/common/error.c
    src/system/power.c
)
target_link_libraries(test_cli_parser PRIVATE powrprof)

wt_add_unit_test(test_json
    tests/test_json.c
    tests/json_link_stubs.c
    src/output/json.c
    src/common/error.c
    src/common/units.c
    src/platform/time.c
    src/platform/console.c
    src/system/privilege.c
    src/system/power.c
)
target_link_libraries(test_json PRIVATE powrprof)

wt_add_unit_test(test_file_identity
    tests/test_file_identity.c
    src/system/file_identity.c
    src/common/error.c
)
target_link_libraries(test_file_identity PRIVATE wintrust version crypt32)

wt_add_unit_test(test_delay_plan
    tests/test_delay_plan.c
    src/actions/delay_plan.c
    src/actions/safe_actions.c
    src/actions/rollback.c
    src/system/startup.c
    src/system/tasks.c
    src/system/boot.c
    src/system/boot_etw.c
    src/system/services.c
    src/system/file_identity.c
    src/system/privilege.c
    src/system/power.c
    src/core/impact_score.c
    src/common/error.c
    src/common/log.c
    src/platform/paths.c
    src/platform/time.c
    src/platform/console.c
)
target_link_libraries(test_delay_plan PRIVATE
    powrprof advapi32 wevtapi taskschd ole32 oleaut32 rpcrt4
    wintrust version crypt32 shell32
)

wt_add_unit_test(test_confidence
    tests/test_confidence.c
    src/core/confidence.c
    src/core/report_model.c
    src/common/error.c
)

wt_add_unit_test(test_tui_compare
    tests/test_tui_compare.c
    src/tui/tui_compare.c
    src/common/error.c
    src/platform/paths.c
)
target_link_libraries(test_tui_compare PRIVATE shell32)

wt_add_unit_test(test_tui_theme
    tests/test_tui_theme.c
    src/tui/tui_theme.c
)

wt_add_unit_test(test_service_policy
    tests/test_service_policy.c
    src/service/service_policy.c
    src/common/error.c
    src/platform/paths.c
)
target_link_libraries(test_service_policy PRIVATE shell32)

wt_add_unit_test(test_service_ipc_acl
    tests/test_service_ipc_acl.c
    src/platform/service_ipc.c
    src/common/error.c
    src/common/log.c
    src/platform/paths.c
)
target_link_libraries(test_service_ipc_acl PRIVATE advapi32 shell32)

wt_add_unit_test(test_doctor_plan
    tests/test_doctor_plan.c
    src/core/doctor_plan.c
    src/core/report_model.c
    src/common/error.c
)

wt_add_unit_test(test_uninstall
    tests/test_uninstall.c
    src/system/uninstall.c
    src/system/startup.c
    src/system/boot.c
    src/system/boot_etw.c
    src/system/services.c
    src/system/file_identity.c
    src/core/impact_score.c
    src/common/error.c
    src/common/log.c
    src/platform/paths.c
    src/platform/time.c
    src/platform/console.c
)
target_link_libraries(test_uninstall PRIVATE
    advapi32 wevtapi wintrust version crypt32 shell32
)

wt_add_unit_test(test_apply_preview
    tests/test_apply_preview.c
    src/actions/apply_preview.c
    src/actions/action_map.c
    src/actions/safe_actions.c
    src/actions/rollback.c
    src/system/power.c
    src/system/startup.c
    src/system/tasks.c
    src/system/boot.c
    src/system/boot_etw.c
    src/system/services.c
    src/system/file_identity.c
    src/system/privilege.c
    src/core/impact_score.c
    src/common/error.c
    src/common/log.c
    src/platform/paths.c
    src/platform/time.c
    src/platform/console.c
)
target_link_libraries(test_apply_preview PRIVATE
    powrprof advapi32 wevtapi taskschd ole32 oleaut32 rpcrt4
    wintrust version crypt32 shell32
)

wt_add_unit_test(test_recommendations
    tests/test_recommendations.c
    src/core/recommendations.c
    src/core/confidence.c
    src/core/impact_score.c
    src/core/report_model.c
    src/common/error.c
    src/common/log.c
    src/platform/paths.c
    src/platform/time.c
    src/system/power.c
    src/system/boot.c
    src/system/boot_etw.c
    src/system/services.c
    src/system/updates.c
    src/system/blockers.c
    src/system/storage_health.c
    src/system/reliability.c
    src/system/startup.c
    src/system/tasks.c
    src/system/file_identity.c
    src/system/uninstall.c
    src/system/os_info.c
)
target_link_libraries(test_recommendations PRIVATE
    powrprof advapi32 wevtapi taskschd ole32 oleaut32 wuguid rstrtmgr
    wintrust version crypt32
)

wt_add_unit_test(test_impact_score
    tests/test_impact_score.c
    src/core/impact_score.c
    src/system/file_identity.c
    src/common/error.c
)
target_link_libraries(test_impact_score PRIVATE wintrust version crypt32)

wt_add_unit_test(test_fleet_pack
    tests/test_fleet_pack.c
    src/common/sha256.c
    src/common/zip_store.c
    src/common/error.c
)
target_link_libraries(test_fleet_pack PRIVATE bcrypt)

add_custom_target(wintune_tests_all
    DEPENDS
        wintune_unit_tests
        test_units
        test_cli_parser
        test_json
        test_recommendations
        test_confidence
        test_tui_compare
        test_tui_theme
        test_service_policy
        test_service_ipc_acl
        test_doctor_plan
        test_uninstall
        test_apply_preview
        test_delay_plan
        test_file_identity
        test_impact_score
        test_fleet_pack
)
