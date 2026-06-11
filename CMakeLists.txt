cmake_minimum_required(VERSION 3.24)
project(wintune C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Phase 0 skeleton sources. Additional modules (metrics, system, output, tui)
# are added to this list as each phase is implemented.
add_executable(wintune
    src/main.c
    src/common/error.c
    src/common/log.c
    src/platform/console.c
    src/cli/cli.c
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

# Windows libraries are linked per module as they are needed:
#   pdh       -> metrics (PDH counters)
#   psapi     -> process inspection
#   powrprof  -> power management
#   advapi32  -> services / registry
#   iphlpapi  -> network adapters
# Phase 0 only relies on kernel32 (linked by default on MSVC).
