# third_party/mbedtls/CMakeLists.txt — EmbedIQ vendored mbedTLS 3.6.1 build
#
# Builds all mbedTLS library sources as a single static library: embediq_mbedtls.
# Only the library/ directory is compiled — no mbedTLS programs or tests.
#
# D-LIB-3: source in third_party/, never modified.
# D-LIB-4: consumers link embediq_mbedtls; no FB ever includes mbedTLS headers.
# License: Apache-2.0 (same as EmbedIQ public repo — compatible).

cmake_minimum_required(VERSION 3.16)

# Collect all mbedTLS library source files
file(GLOB MBEDTLS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/library/*.c"
)

add_library(embediq_mbedtls STATIC ${MBEDTLS_SOURCES})

target_include_directories(embediq_mbedtls PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Use the EmbedIQ-specific mbedTLS configuration file.
# This macro tells mbedTLS to include our config instead of its default.
target_compile_definitions(embediq_mbedtls PUBLIC
    MBEDTLS_CONFIG_FILE="mbedtls_embediq_posix_config.h"
)

# mbedTLS generates some warnings on strict compilers — suppress them
# since we do not own this source (D-LIB-3: never modify third_party/).
target_compile_options(embediq_mbedtls PRIVATE
    -Wno-unused-parameter
    -Wno-sign-conversion
    -Wno-conversion
)
