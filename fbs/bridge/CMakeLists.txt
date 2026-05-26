# fbs/bridge/CMakeLists.txt — Bridge daemon Service FB static library
#
# Item 5a — fb_bridge is a Service FB (boot_phase = EMBEDIQ_BOOT_PHASE_BRIDGE = 4)
# that mediates the EmbedIQ bus and External FBs over two compile-time-selected
# transports: a POSIX socket (Linux) or an in-process OSAL queue (RTOS / unit
# tests). Same library, same registration entry point — different transport
# dispatch path inside fb_bridge.c and embediq_ext_fb.c.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

add_library(embediq_bridge STATIC
    fb_bridge.c
    ${CMAKE_SOURCE_DIR}/core/src/bridge/embediq_ext_fb.c
)

target_include_directories(embediq_bridge PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(embediq_bridge PRIVATE
    embediq_fb_engine
)

# Default transport selection for the library. Item 5a unit tests exercise
# the in-process queue path; Item 5c integration tests will revisit this
# (likely by adding a second library variant compiled with the socket
# transport). PUBLIC so consumers see the same transport their library was
# compiled with — no mismatched conditional layout across TUs.
target_compile_definitions(embediq_bridge PUBLIC
    EMBEDIQ_BRIDGE_TRANSPORT_QUEUE
)

target_compile_options(embediq_bridge PRIVATE
    -Wall
    -Wextra
    -Werror
)
