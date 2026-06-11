cmake_minimum_required(VERSION 3.24)
project(wintune C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

    # metrics
    src/metrics/pdh_utils.c
    src/metrics/cpu.c
    src/metrics/memory.c
    src/metrics/disk.c
    src/metrics/process.c

    # system
    src/system/os_info.c

    # core
    src/core/report_model.c
    src/core/scan.c

    # output
    src/output/table.c
    src/output/text.c
    src/output/json.c

    # cli
    src/cli/cli.c
    src/cli/commands_scan.c
    src/cli/commands_top.c
)

target_include_directories(wintune PRIVATE src)

target_compile_definitions(wintune PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(wintune PRIVATE /W4 /permissive-)
endif()

# Windows libraries linked as modules require them:
#   pdh      -> PDH performance counters (cpu)
#   psapi    -> process memory/IO counters (process)
#   advapi32 -> registry (os_info), services later
# Additional libs (powrprof, iphlpapi) are added in later phases.
target_link_libraries(wintune PRIVATE
    pdh
    psapi
    advapi32
)
