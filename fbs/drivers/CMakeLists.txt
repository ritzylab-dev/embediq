# fbs/drivers/CMakeLists.txt — Driver FB static library
#
# Builds embediq_driver_fbs from portable Driver FB translation units.
# Depends on embediq_fb_engine (framework APIs) only.
# The HAL implementation (embediq_hal_posix etc.) is linked by the application
# or test executable — not by this library — keeping it platform-agnostic.

add_library(embediq_driver_fbs STATIC
    fb_timer.c
    fb_nvm.c
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
