# platform/posix/CMakeLists.txt — POSIX Platform FB static library
#
# Builds embediq_platform_posix from the three Platform FB translation units.
# This library depends on embediq_fb_engine (for bus/engine APIs) and
# embediq_osal_posix (for threading, mutexes, delay, and time calls).
#
# Tests that exercise these FBs link against embediq_platform_posix.

add_library(embediq_platform_posix STATIC
    fb_timer.c
    fb_watchdog.c
    fb_nvm.c
)

target_include_directories(embediq_platform_posix PUBLIC
    ${CMAKE_SOURCE_DIR}/platform/posix
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(embediq_platform_posix PUBLIC
    embediq_fb_engine
)

target_compile_options(embediq_platform_posix PRIVATE
    -Wall
    -Wextra
    -Werror
)
