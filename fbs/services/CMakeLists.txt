# fbs/services/CMakeLists.txt — Service FB static library
#
# Builds embediq_service_fbs from portable Service FB translation units.
# Service FBs are platform-agnostic: zero HAL includes, zero OSAL calls.
# They communicate with the outside world exclusively via message bus
# and ops tables registered at boot.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

add_library(embediq_service_fbs STATIC
    fb_telemetry.c
)

target_include_directories(embediq_service_fbs PUBLIC
    ${CMAKE_SOURCE_DIR}/fbs/services
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/generated
)

target_link_libraries(embediq_service_fbs PUBLIC
    embediq_fb_engine
)

target_compile_options(embediq_service_fbs PRIVATE
    -Wall
    -Wextra
    -Werror
)
