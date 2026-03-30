# examples/gateway/CMakeLists.txt — EmbedIQ Industrial Edge Gateway Demo

add_executable(gateway
    main.c
    fb_field_receiver.c
    fb_policy_engine.c
    fb_north_bridge.c
)

set_target_properties(gateway PROPERTIES OUTPUT_NAME embediq_gateway)

target_include_directories(gateway PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/examples/gateway
)

target_link_libraries(gateway PRIVATE
    embediq_driver_fbs
)

target_compile_options(gateway PRIVATE
    -Wall
    -Wextra
    -Werror
)
