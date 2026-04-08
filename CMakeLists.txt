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
# libembediq_obs — standalone Observatory INTERFACE target (Decision E)
#
# Zero-dependency target: consumers can link only the Observatory header
# without pulling in the FB engine, OSAL, HAL, or any other EmbedIQ component.
#
# Usage in an external project:
#   find_package(embediq REQUIRED)
#   target_link_libraries(my_app PRIVATE embediq_obs)
#
# The INTERFACE target exposes only the public header directory. The actual
# obs.c implementation (which depends on OSAL) is in embediq_fb_engine for
# full EmbedIQ builds, and is expected to be provided as a thin shim by the
# consumer in standalone / Observatory-only deployments.
#
# Zero-dependency contract (E2-06): this target MUST NOT link against
# embediq_osal_posix, embediq_hal_posix, embediq_fb_engine, or any other
# EmbedIQ component. Any violation of this constraint is a build error
# in existing product retrofit ("Observatory Only") deployments.
# ---------------------------------------------------------------------------
add_library(embediq_obs INTERFACE)
target_include_directories(embediq_obs INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/core/include>
    $<INSTALL_INTERFACE:include>
)

# Verify zero-dependency contract at configure time
get_target_property(_obs_deps embediq_obs INTERFACE_LINK_LIBRARIES)
if(_obs_deps)
    message(FATAL_ERROR
        "embediq_obs INTERFACE target must have zero dependencies. "
        "Found: ${_obs_deps}. Decision E zero-dependency constraint violated.")
endif()

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
    add_subdirectory(components/embediq_crc)
    add_subdirectory(tests/unit)
    add_subdirectory(tests/lib)
    add_subdirectory(tests/integration)
endif()

# ---------------------------------------------------------------------------
# CLI tool tests (Python)
# ---------------------------------------------------------------------------
add_test(NAME cli_obs_tool
    COMMAND ${CMAKE_COMMAND} -E env
        python3
        ${CMAKE_SOURCE_DIR}/tests/cli/test_obs_cli.py)
