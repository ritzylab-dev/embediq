# hal/posix/CMakeLists.txt — POSIX HAL implementation library
#
# Builds embediq_hal_posix from:
#   peripherals/ — HAL peripheral implementations (timer, flash, wdg, obs_stream)
#   ops/         — Ops table implementations (TLS stub — OpenSSL impl in Phase 2)
#
# Peripheral files: zero embediq_*.h includes (layer boundary enforced by boundary_checker).
# Ops files: include ops table headers from core/include/ops/ — this is correct.
# The ops table implementation bridges the HAL and ops contract layers by design.

add_library(embediq_hal_posix STATIC
    peripherals/hal_timer_posix.c
    peripherals/hal_flash_posix.c
    peripherals/hal_wdg_posix.c
    peripherals/hal_obs_stream_posix.c
    ops/hal_tls_posix.c
)

target_include_directories(embediq_hal_posix PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include/hal
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/hal/posix/ops
)

target_compile_options(embediq_hal_posix PRIVATE
    -Wall
    -Wextra
    -Werror
)
