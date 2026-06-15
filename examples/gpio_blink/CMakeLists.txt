# examples/gpio_blink/CMakeLists.txt — EmbedIQ GPIO Blink Demo

add_executable(gpio_blink
    main.c
    fb_blink_led.c
)

set_target_properties(gpio_blink PROPERTIES OUTPUT_NAME embediq_gpio_blink)

target_include_directories(gpio_blink PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/fbs/drivers          # fb_gpio.h
    ${CMAKE_SOURCE_DIR}/examples/gpio_blink
)

target_link_libraries(gpio_blink PRIVATE
    embediq_driver_fbs
    embediq_hal_posix
)

target_compile_options(gpio_blink PRIVATE
    -Wall
    -Wextra
    -Werror
)
