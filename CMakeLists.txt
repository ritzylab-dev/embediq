cmake_minimum_required(VERSION 3.16)
project(embediq C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Platform selection — REQUIRED
#
# Supported values:
#   host   — POSIX/Linux/macOS simulation (Phase 1 primary target)
#   test   — same as host, additionally enables test infrastructure
#
# Usage:
#   cmake -B build -DEMBEDIQ_PLATFORM=host
#   cmake -B build -DEMBEDIQ_PLATFORM=test
# ---------------------------------------------------------------------------
if(NOT DEFINED EMBEDIQ_PLATFORM)
    message(FATAL_ERROR "Set -DEMBEDIQ_PLATFORM=host or test")
endif()

if(NOT EMBEDIQ_PLATFORM STREQUAL "host" AND NOT EMBEDIQ_PLATFORM STREQUAL "test")
    message(FATAL_ERROR "Set -DEMBEDIQ_PLATFORM=host or test")
endif()

# ---------------------------------------------------------------------------
# Compile-time platform definitions
# ---------------------------------------------------------------------------
if(EMBEDIQ_PLATFORM STREQUAL "host")
    add_compile_definitions(EMBEDIQ_PLATFORM_HOST)
endif()

if(EMBEDIQ_PLATFORM STREQUAL "test")
    add_compile_definitions(EMBEDIQ_PLATFORM_HOST EMBEDIQ_PLATFORM_TEST)
endif()

# ---------------------------------------------------------------------------
# Include paths
#
#   core/include  — framework contract headers; added as SYSTEM so -Wall/-Werror
#                   do not fire on framework-internal headers.
#   generated/    — output directory for messages.iq → embediq_msg_catalog.h
#   osal/posix/   — POSIX threading abstraction (host + test platforms only)
# ---------------------------------------------------------------------------
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/core/include)
include_directories(${CMAKE_SOURCE_DIR}/generated)

if(EMBEDIQ_PLATFORM STREQUAL "host" OR EMBEDIQ_PLATFORM STREQUAL "test")
    include_directories(${CMAKE_SOURCE_DIR}/osal/posix)
endif()

# ---------------------------------------------------------------------------
# Test infrastructure (ctest)
# ---------------------------------------------------------------------------
enable_testing()

add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running EmbedIQ test suite"
)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(examples/thermostat)
add_subdirectory(examples/gateway)

if(EMBEDIQ_PLATFORM STREQUAL "host" OR EMBEDIQ_PLATFORM STREQUAL "test")
    add_subdirectory(osal/posix)
    add_subdirectory(core/src)
    add_subdirectory(hal/posix)
    add_subdirectory(fbs/drivers)
    add_subdirectory(tests/unit)
    add_subdirectory(tests/integration)
endif()

# ---------------------------------------------------------------------------
# CLI tool tests (Python)
# ---------------------------------------------------------------------------
add_test(NAME cli_obs_tool
    COMMAND ${CMAKE_COMMAND} -E env
        python3
        ${CMAKE_SOURCE_DIR}/tests/cli/test_obs_cli.py)
