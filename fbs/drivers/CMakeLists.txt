# fbs/drivers/CMakeLists.txt — Driver FB static library
#
# Builds embediq_driver_fbs from portable Driver FB translation units.
# Depends on embediq_fb_engine (framework APIs) only.
# The HAL implementation (embediq_hal_posix etc.) is linked by the application
# or test executable — not by this library — keeping it platform-agnostic.

add_library(embediq_driver_fbs STATIC
    fb_timer.c
    fb_nvm.c
    fb_watchdog.c
)

target_include_directories(embediq_driver_fbs PUBLIC
    ${CMAKE_SOURCE_DIR}/fbs/drivers
)

target_link_libraries(embediq_driver_fbs PUBLIC
    embediq_fb_engine
)

target_compile_options(embediq_driver_fbs PRIVATE
    -Wall
    -Wextra
    -Werror
)

# XOBS-4: Build driver FBs at trace level 2 so that fb_watchdog.c
# is compiled with EMBEDIQ_TRACE_TIMING=1. At the default host level
# (level 1), EMBEDIQ_OBS_EMIT_TIMING expands to ((void)0) — TIMING
# events are never emitted, making test_wdg_checkin_emits_timing_event
# pass vacuously. This definition must be on the LIBRARY, not only
# on the test binary, because the macro expansion happens when
# fb_watchdog.c is compiled, not when test_fb_watchdog.c is compiled.
target_compile_definitions(embediq_driver_fbs PRIVATE EMBEDIQ_TRACE_LEVEL=2)
