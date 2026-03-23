# hal/posix/CMakeLists.txt — POSIX HAL implementation library
#
# Builds embediq_hal_posix from POSIX-specific HAL translation units.
# Only depends on C stdlib (no EmbedIQ framework headers).
# HAL contract headers (core/include/hal/) are added as PRIVATE includes.

add_library(embediq_hal_posix STATIC
    hal_timer_posix.c
    hal_flash_posix.c
)

target_include_directories(embediq_hal_posix PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include/hal
)

target_compile_options(embediq_hal_posix PRIVATE
    -Wall
    -Wextra
    -Werror
)
