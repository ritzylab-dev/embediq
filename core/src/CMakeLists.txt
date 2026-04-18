# core/src/CMakeLists.txt — FB engine static library
#
# Source files live in per-module subdirectories per AGENTS.md §5.
# R-02: no malloc/free. Host/test builds expose fb_engine__* test helpers
# via the EMBEDIQ_PLATFORM_HOST compile definition (set by root CMakeLists).

add_library(embediq_fb_engine STATIC
    registry/fb_engine.c
    bus/message_bus.c
    fsm/fsm_engine.c
    observatory/obs.c
    embediq_platform.c
)

target_include_directories(embediq_fb_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/hal
)

target_link_libraries(embediq_fb_engine PUBLIC embediq_osal_posix embediq_hal_posix)

target_compile_options(embediq_fb_engine PRIVATE
    -Wall
    -Wextra
    -Werror
)

# ---------------------------------------------------------------------------
# embediq_cfg — typed configuration helper (C1)
#
# Thin wrapper over embediq_nvm.h. Lives in core/src/ because it depends on
# a framework contract (embediq_nvm.h), which components/ may not include.
# The test/application binary links both embediq_cfg and the fb_nvm impl
# (embediq_driver_fbs) to satisfy the embediq_nvm_* externs at final link.
# ---------------------------------------------------------------------------

add_library(embediq_cfg STATIC
    cfg/embediq_cfg.c
)

target_include_directories(embediq_cfg PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
)

target_compile_options(embediq_cfg PRIVATE
    -Wall
    -Wextra
    -Werror
)
