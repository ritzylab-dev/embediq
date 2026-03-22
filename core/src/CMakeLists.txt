# core/src/CMakeLists.txt — FB engine static library
#
# Builds embediq_fb_engine from a single translation unit.
# R-02: no malloc/free. Host/test builds expose fb_engine__* test helpers
# via the EMBEDIQ_PLATFORM_HOST compile definition (set by root CMakeLists).

add_library(embediq_fb_engine STATIC fb_engine.c)

target_include_directories(embediq_fb_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
)

target_compile_options(embediq_fb_engine PRIVATE
    -Wall
    -Wextra
    -Werror
)
