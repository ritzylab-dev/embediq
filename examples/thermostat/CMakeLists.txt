# examples/thermostat/CMakeLists.txt — EmbedIQ Smart Thermostat Demo

add_executable(thermostat
    main.c
    fb_temp_sensor.c
    fb_temp_controller.c
)

set_target_properties(thermostat PROPERTIES OUTPUT_NAME embediq_thermostat)

target_include_directories(thermostat PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/platform/posix
    ${CMAKE_SOURCE_DIR}/examples/thermostat
)

target_link_libraries(thermostat PRIVATE embediq_platform_posix)

target_compile_options(thermostat PRIVATE
    -Wall
    -Wextra
    -Werror
)
